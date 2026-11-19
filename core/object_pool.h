#ifndef CORE_OBJECT_POOL_H_
#define CORE_OBJECT_POOL_H_

// An object pool for one acquiring thread and multiple releasing threads.
// Blocks grow dynamically and each block has a bounded MPSC free queue.
// Callers may build caches on top of the batch APIs.

#include "../basic/concurrency/queue/mpsc_queue.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

template <typename T, std::size_t BlockCapacity = 1024,
          std::size_t QueueCapacity = 4096>
class object_pool {
    static_assert(BlockCapacity >= 2 &&
                      (BlockCapacity & (BlockCapacity - 1)) == 0,
                  "BlockCapacity must be a power of two and at least two");
    static_assert(QueueCapacity >= 2 &&
                      (QueueCapacity & (QueueCapacity - 1)) == 0,
                  "QueueCapacity must be a power of two and at least two");
    static_assert(QueueCapacity >= BlockCapacity,
                  "QueueCapacity must hold one complete block");
    static_assert(std::is_nothrow_destructible<T>::value,
                  "T must have a noexcept destructor");

    struct Slot {
        alignas(T) unsigned char storage[sizeof(T)];
        std::atomic<bool> constructed{false};
        std::atomic<bool> in_use{false};
        MpscQueue<Slot *, QueueCapacity> *queue{nullptr};

        T *object() noexcept {
            return std::launder(reinterpret_cast<T *>(storage));
        }
    };

    struct Block {
        MpscQueue<Slot *, QueueCapacity> free;
        std::array<Slot, BlockCapacity> slots;
        std::atomic<Block *> next{nullptr};

        ~Block() noexcept {
            for (Slot &slot : slots) {
                if (slot.constructed.exchange(false,
                                               std::memory_order_relaxed)) {
                    slot.object()->~T();
                }
            }
        }
    };

public:
    object_pool() = default;
    ~object_pool() { clear(); }

    object_pool(const object_pool &) = delete;
    object_pool &operator=(const object_pool &) = delete;

    // Constructs up to count objects and publishes them to the MPSC free
    // queues. This is intended for an initialization or provisioning path.
    // The same constructor arguments are used for every object.
    template <typename... Args>
    std::size_t create_batch(std::size_t count, Args &&...args) {
        static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                      "T's pool constructor must be noexcept");

        if (count == 0) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(expand_mutex_);
        std::size_t created = 0;

        while (created < count) {
            auto block = std::make_unique<Block>();
            const std::size_t in_block =
                std::min(BlockCapacity, count - created);

            for (std::size_t i = 0; i < in_block; ++i) {
                Slot &slot = block->slots[i];
                slot.queue = &block->free;
                construct(&slot, std::forward<Args>(args)...);
                slot.constructed.store(true, std::memory_order_relaxed);
                slot.in_use.store(false, std::memory_order_relaxed);
                if (!block->free.try_push(&slot)) {
                    slot.object()->~T();
                    slot.constructed.store(false, std::memory_order_relaxed);
                    return created;
                }
                ++created;
            }

            publish_block(std::move(block));
            capacity_.fetch_add(in_block, std::memory_order_relaxed);
            free_count_.fetch_add(in_block, std::memory_order_release);
        }

        return created;
    }

    // Adds one full block. Useful for a low-watermark monitor that wants
    // equal-sized expansion without exposing constructor details repeatedly.
    template <typename... Args>
    std::size_t expand_one_block(Args &&...args) {
        return create_batch(BlockCapacity, std::forward<Args>(args)...);
    }

    // Expands only when the currently available count is below low_watermark.
    // expansion_count is the number of objects to provision, not bytes.
    template <typename... Args>
    std::size_t expand_if_below_low_watermark(std::size_t low_watermark,
                                              std::size_t expansion_count,
                                              Args &&...args) {
        if (free_count_.load(std::memory_order_acquire) >= low_watermark) {
            return 0;
        }
        return create_batch(expansion_count,
                            std::forward<Args>(args)...);
    }

    // Only the single acquiring thread may call try_acquire/try_acquire_batch.
    // An empty pool returns false; expansion is explicit.
    bool try_acquire(T *&value) noexcept {
        Slot *slot = nullptr;
        for (Block *block = head_.load(std::memory_order_acquire);
             block != nullptr;
             block = block->next.load(std::memory_order_acquire)) {
            if (block->free.try_pop(slot)) {
                free_count_.fetch_sub(1, std::memory_order_release);
                slot->in_use.store(true, std::memory_order_release);
                value = slot->object();
                return true;
            }
        }
        value = nullptr;
        return false;
    }

    std::size_t try_acquire_batch(T **values, std::size_t count) noexcept {
        if (values == nullptr) {
            return 0;
        }

        std::size_t acquired = 0;
        while (acquired < count && try_acquire(values[acquired])) {
            ++acquired;
        }
        return acquired;
    }

    // Multiple threads may release concurrently. value must belong to this
    // pool and be checked out; release preserves the constructed object.
    bool try_release(T *value) noexcept {
        if (value == nullptr) {
            return false;
        }

        Slot *slot = slot_from_object(value);
        if (!slot->constructed.load(std::memory_order_acquire) ||
            slot->queue == nullptr) {
            return false;
        }

        bool expected = true;
        if (!slot->in_use.compare_exchange_strong(
                expected, false, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return false;
        }

        if (!slot->queue->try_push(slot)) {
            slot->in_use.store(true, std::memory_order_release);
            return false;
        }

        free_count_.fetch_add(1, std::memory_order_release);
        return true;
    }

    std::size_t try_release_batch(T *const *values, std::size_t count) noexcept {
        if (values == nullptr) {
            return 0;
        }

        std::size_t released = 0;
        while (released < count && try_release(values[released])) {
            ++released;
        }
        return released;
    }

    std::size_t capacity() const noexcept {
        return capacity_.load(std::memory_order_acquire);
    }

    std::size_t available() const noexcept {
        return free_count_.load(std::memory_order_acquire);
    }

    std::size_t in_use() const noexcept {
        return capacity() - available();
    }

    // All users must stop accessing the pool before clear(). Objects that are
    // still checked out are destroyed as part of pool teardown.
    void clear() noexcept {
        std::lock_guard<std::mutex> lock(expand_mutex_);
        for (auto &block : blocks_) {
            for (Slot &slot : block->slots) {
                if (slot.constructed.load(std::memory_order_relaxed)) {
                    slot.object()->~T();
                    slot.constructed.store(false, std::memory_order_relaxed);
                    slot.in_use.store(false, std::memory_order_relaxed);
                }
            }
        }
        blocks_.clear();
        head_.store(nullptr, std::memory_order_release);
        capacity_.store(0, std::memory_order_release);
        free_count_.store(0, std::memory_order_release);
    }

private:
    template <typename... Args>
    static T *construct(Slot *slot, Args &&...args) noexcept {
        return ::new (static_cast<void *>(slot->storage))
            T(std::forward<Args>(args)...);
    }

    static T *object_at(Slot *slot) noexcept {
        return slot->object();
    }

    static Slot *slot_from_object(T *value) noexcept {
        // storage is the first member of Slot and has the same address.
        return reinterpret_cast<Slot *>(
            reinterpret_cast<unsigned char *>(value));
    }

    void publish_block(std::unique_ptr<Block> block) {
        Block *raw = block.get();
        blocks_.push_back(std::move(block));

        Block *head = head_.load(std::memory_order_relaxed);
        do {
            raw->next.store(head, std::memory_order_relaxed);
        } while (!head_.compare_exchange_weak(
            head, raw, std::memory_order_release,
            std::memory_order_relaxed));
    }

private:
    // Blocks are only appended and are never moved; readers traverse them via
    // head_/next. The vector itself is touched only by expansion and clear.
    std::vector<std::unique_ptr<Block>> blocks_;
    std::atomic<Block *> head_{nullptr};
    std::atomic<std::size_t> capacity_{0};
    std::atomic<std::size_t> free_count_{0};
    mutable std::mutex expand_mutex_;
};

#endif  // CORE_OBJECT_POOL_H_

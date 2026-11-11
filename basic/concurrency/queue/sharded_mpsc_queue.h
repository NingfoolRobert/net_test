#ifndef BASIC_SHARDED_MPSC_QUEUE_H_
#define BASIC_SHARDED_MPSC_QUEUE_H_

#include "../spin_lock.h"
#include "ring_buffer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

// A low-latency MPSC queue built from independent SPSC ring buffers.
//
// Each producer must register once and use its ProducerToken from the same
// thread for all subsequent writes. Registration hashes std::thread::id to a
// starting shard and uses linear probing to resolve collisions. The consumer
// is single-threaded and round-robins over active shards.
//
// This queue is bounded by ShardCount * RingCapacity. It preserves FIFO order
// within each producer, but intentionally does not promise a global order
// across producers.
template <typename T, std::size_t ShardCount, std::size_t RingCapacity>
class ShardedMpscQueue {
    static_assert(ShardCount >= 2 && (ShardCount & (ShardCount - 1)) == 0,
                  "ShardCount must be a power of two and at least two");
    static_assert(ShardCount <= 64,
                  "active bitmap supports at most 64 shards");

    struct Shard {
        RingBuffer<T, RingCapacity> ring;
        std::thread::id owner;
        std::size_t owner_hash = 0;
        bool registered = false;
        std::atomic<bool> active{false};
    };

public:
    class ProducerToken {
        friend class ShardedMpscQueue;

    public:
        ProducerToken() noexcept = default;

        bool valid() const noexcept {
            return queue_ != nullptr;
        }

    private:
        ProducerToken(ShardedMpscQueue *queue, std::size_t shard) noexcept
            : queue_(queue), shard_(shard) {}

        ShardedMpscQueue *queue_ = nullptr;
        std::size_t shard_ = 0;
    };

    ShardedMpscQueue() = default;

    ShardedMpscQueue(const ShardedMpscQueue &) = delete;
    ShardedMpscQueue &operator=(const ShardedMpscQueue &) = delete;

    // Registration is cold-path work. The returned token must stay on the
    // registering thread and must not outlive this queue.
    ProducerToken register_producer() noexcept {
        const std::thread::id id = std::this_thread::get_id();
        const std::size_t hash = std::hash<std::thread::id>{}(id);
        std::unique_lock<SpinLock> lock(registration_lock_);

        const std::size_t start = hash & (ShardCount - 1);
        for (std::size_t probe = 0; probe < ShardCount; ++probe) {
            const std::size_t index = (start + probe) & (ShardCount - 1);
            Shard &shard = shards_[index];

            if (shard.registered && shard.owner == id) {
                return ProducerToken{this, index};
            }
            if (!shard.registered) {
                shard.owner = id;
                shard.owner_hash = hash;
                shard.registered = true;
                return ProducerToken{this, index};
            }
        }

        return ProducerToken{};
    }

    bool try_push(ProducerToken &token, const T &value) {
        Shard *shard = shard_for(token);
        if (shard == nullptr) {
            return false;
        }
        const bool was_active =
            shard->active.load(std::memory_order_relaxed);
        if (!shard->ring.try_push(value)) {
            return false;
        }
        publish_active(token.shard_, *shard, was_active);
        return true;
    }

    // Hot path convenience overload. The first call on a thread performs
    // registration; later calls use only the thread-local shard token.
    bool try_push(const T &value) {
        ProducerToken &token = local_token();
        return try_push(token, value);
    }

    bool try_push(ProducerToken &token, T &&value) {
        Shard *shard = shard_for(token);
        if (shard == nullptr) {
            return false;
        }
        const bool was_active =
            shard->active.load(std::memory_order_relaxed);
        if (!shard->ring.try_push(std::move(value))) {
            return false;
        }
        publish_active(token.shard_, *shard, was_active);
        return true;
    }

    bool try_push(T &&value) {
        ProducerToken &token = local_token();
        return try_push(token, std::move(value));
    }

    template <typename... Args>
    bool try_emplace(ProducerToken &token, Args &&...args) {
        Shard *shard = shard_for(token);
        if (shard == nullptr) {
            return false;
        }
        const bool was_active =
            shard->active.load(std::memory_order_relaxed);
        if (!shard->ring.try_emplace(std::forward<Args>(args)...)) {
            return false;
        }
        publish_active(token.shard_, *shard, was_active);
        return true;
    }

    template <typename... Args>
    bool try_emplace(Args &&...args) {
        ProducerToken &token = local_token();
        return try_emplace(token, std::forward<Args>(args)...);
    }

    // Call from the single consumer thread only.
    bool try_pop(T &value) {
        std::uint64_t pending = active_bitmap_.load(std::memory_order_acquire);
        const std::size_t first = consumer_cursor_;
        const std::uint64_t rotated =
            (pending >> first) | (pending << ((64 - first) & 63));

        pending = rotated;
        while (pending != 0) {
            const std::size_t offset =
                static_cast<std::size_t>(__builtin_ctzll(pending));
            const std::size_t index = (first + offset) & (ShardCount - 1);
            Shard &shard = shards_[index];
            if (shard.ring.try_pop(value)) {
                consumer_cursor_ = (index + 1) & (ShardCount - 1);
                clear_active_if_empty(index, shard);
                return true;
            }
            clear_active_if_empty(index, shard);
            pending &= pending - 1;
        }
        return false;
    }

    bool empty() const noexcept {
        return active_bitmap_.load(std::memory_order_acquire) == 0;
    }

private:
    static constexpr std::uint64_t bit_for(std::size_t index) noexcept {
        return std::uint64_t{1} << index;
    }

    void publish_active(std::size_t index,
                        Shard &shard,
                        bool was_active) noexcept {
        // The common path only reads active. The bitmap RMW is performed on
        // an empty-to-active transition, not once per message.
        if (!was_active ||
            !shard.active.load(std::memory_order_acquire)) {
            bool expected = false;
            if (shard.active.compare_exchange_strong(
                    expected, true, std::memory_order_release,
                    std::memory_order_relaxed)) {
                active_bitmap_.fetch_or(bit_for(index),
                                        std::memory_order_release);
            }
        }
    }

    void activate_if_needed(std::size_t index, Shard &shard) noexcept {
        bool expected = false;
        if (shard.active.compare_exchange_strong(
                expected, true, std::memory_order_release,
                std::memory_order_relaxed)) {
            active_bitmap_.fetch_or(bit_for(index),
                                    std::memory_order_release);
        }
    }

    void clear_active_if_empty(std::size_t index, Shard &shard) noexcept {
        const std::uint64_t bit = bit_for(index);
        // Clear the bitmap before active. A producer that races with this
        // transition will either see active=true and the recheck below finds
        // its item, or see active=false and activate the shard itself.
        active_bitmap_.fetch_and(~bit, std::memory_order_acq_rel);
        shard.active.store(false, std::memory_order_release);

        // A producer may have pushed while the bit was being cleared. Restore
        // it if an item is visible after the clear.
        if (!shard.ring.is_empty()) {
            activate_if_needed(index, shard);
        }
    }

    ProducerToken &local_token() noexcept {
        if (tls_token_.queue_ != this) {
            tls_token_ = register_producer();
        }
        return tls_token_;
    }

    Shard *shard_for(ProducerToken &token) noexcept {
        if (token.queue_ != this || token.shard_ >= ShardCount) {
            return nullptr;
        }
        return &shards_[token.shard_];
    }

    std::array<Shard, ShardCount> shards_;
    SpinLock registration_lock_;
    alignas(64) std::atomic<std::uint64_t> active_bitmap_{0};
    std::size_t consumer_cursor_ = 0;

    inline static thread_local ProducerToken tls_token_{};
};

#endif  // BASIC_SHARDED_MPSC_QUEUE_H_

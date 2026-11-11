#ifndef BASIC_MPMC_QUEUE_H_
#define BASIC_MPMC_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

// A bounded multi-producer, multi-consumer queue.
//
// The queue does not allocate after construction. Capacity must be a power of
// two. try_push()/try_emplace() and try_pop() are non-blocking; pop() spins
// until an item is available. The queue must be quiescent before destruction.
//
// This is based on a per-cell sequence number. Producers and consumers only
// contend on their respective position counters, while the sequence provides
// the release/acquire hand-off for the object stored in a cell.
template <typename T, std::size_t Capacity>
class MpmcQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two and at least two");
    static_assert(Capacity <= (std::size_t{1} << 63),
                  "Capacity must fit in a signed 64-bit sequence distance");

    struct Cell {
        std::atomic<std::uint64_t> sequence{0};
        typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;

        T *object() noexcept {
            return std::launder(reinterpret_cast<T *>(&storage));
        }
    };

public:
    MpmcQueue() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MpmcQueue() noexcept {
        // Destruction while another thread accesses the queue is undefined.
        // At quiescence, every position between dequeue and enqueue owns a
        // live object. Both counters may wrap, so compare positions directly.
        const std::uint64_t end =
            enqueue_pos_.load(std::memory_order_relaxed);
        for (std::uint64_t position =
                 dequeue_pos_.load(std::memory_order_relaxed);
             position != end; ++position) {
            cells_[position & (Capacity - 1)].object()->~T();
        }
    }

    MpmcQueue(const MpmcQueue &) = delete;
    MpmcQueue &operator=(const MpmcQueue &) = delete;
    MpmcQueue(MpmcQueue &&) = delete;
    MpmcQueue &operator=(MpmcQueue &&) = delete;

    bool try_push(const T &value) noexcept(
        std::is_nothrow_copy_constructible<T>::value) {
        return try_emplace(value);
    }

    bool try_push(T &&value) noexcept(
        std::is_nothrow_move_constructible<T>::value) {
        return try_emplace(std::move(value));
    }

    template <typename... Args>
    bool try_emplace(Args &&...args) noexcept(
        std::is_nothrow_constructible<T, Args &&...>::value) {
        static_assert(std::is_nothrow_constructible<T, Args &&...>::value,
                      "MpmcQueue requires a noexcept element constructor");

        std::uint64_t position = enqueue_pos_.load(std::memory_order_relaxed);
        Cell *cell = nullptr;
        for (;;) {
            cell = &cells_[position & (Capacity - 1)];
            const std::uint64_t sequence =
                cell->sequence.load(std::memory_order_acquire);
            const std::int64_t difference =
                static_cast<std::int64_t>(sequence - position);

            if (difference == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        ::new (static_cast<void *>(&cell->storage))
            T(std::forward<Args>(args)...);
        cell->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    // Reserve a contiguous batch with one position CAS. On success, cells are
    // published one by one; consumers may observe a prefix before the whole
    // batch is published. On failure the input is untouched. Count must not
    // exceed Capacity; a zero-length batch succeeds immediately.
    bool try_push_bulk(const T *values, std::size_t count) noexcept {
        static_assert(std::is_nothrow_copy_constructible<T>::value,
                      "MpmcQueue bulk push requires a noexcept copy constructor");
        if (count == 0) {
            return true;
        }
        if (count > Capacity) {
            return false;
        }

        std::uint64_t position = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            bool retry = false;
            for (std::size_t offset = 0; offset < count; ++offset) {
                const std::uint64_t current = position + offset;
                const Cell &cell = cells_[current & (Capacity - 1)];
                const std::uint64_t sequence =
                    cell.sequence.load(std::memory_order_acquire);
                const std::int64_t difference =
                    static_cast<std::int64_t>(sequence - current);
                if (difference < 0) {
                    return false;
                }
                if (difference > 0) {
                    retry = true;
                    break;
                }
            }
            if (retry) {
                position = enqueue_pos_.load(std::memory_order_relaxed);
                continue;
            }
            if (enqueue_pos_.compare_exchange_weak(
                    position, position + count,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;
            }
        }

        for (std::size_t offset = 0; offset < count; ++offset) {
            Cell &cell = cells_[(position + offset) & (Capacity - 1)];
            ::new (static_cast<void *>(&cell.storage)) T(values[offset]);
            cell.sequence.store(position + offset + 1,
                                std::memory_order_release);
        }
        return true;
    }

    bool try_pop(T &value) noexcept(
        std::is_nothrow_move_assignable<T>::value) {
        static_assert(std::is_nothrow_move_assignable<T>::value,
                      "MpmcQueue requires a noexcept move assignment");

        std::uint64_t position = dequeue_pos_.load(std::memory_order_relaxed);
        Cell *cell = nullptr;
        for (;;) {
            cell = &cells_[position & (Capacity - 1)];
            const std::uint64_t sequence =
                cell->sequence.load(std::memory_order_acquire);
            const std::int64_t difference = static_cast<std::int64_t>(
                sequence - (position + 1));

            if (difference == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        value = std::move(*cell->object());
        cell->object()->~T();
        cell->sequence.store(position + Capacity, std::memory_order_release);
        return true;
    }

    // Reserve a contiguous batch with one position CAS, then consume each
    // cell. False leaves the output untouched. The caller must provide count
    // live Ts. Count must not exceed Capacity.
    bool try_pop_bulk(T *values, std::size_t count) noexcept {
        static_assert(std::is_nothrow_move_assignable<T>::value,
                      "MpmcQueue bulk pop requires noexcept move assignment");
        if (count == 0) {
            return true;
        }
        if (count > Capacity) {
            return false;
        }

        std::uint64_t position = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            bool retry = false;
            for (std::size_t offset = 0; offset < count; ++offset) {
                const std::uint64_t current = position + offset;
                const Cell &cell = cells_[current & (Capacity - 1)];
                const std::uint64_t sequence =
                    cell.sequence.load(std::memory_order_acquire);
                const std::int64_t difference =
                    static_cast<std::int64_t>(sequence - (current + 1));
                if (difference < 0) {
                    return false;
                }
                if (difference > 0) {
                    retry = true;
                    break;
                }
            }
            if (retry) {
                position = dequeue_pos_.load(std::memory_order_relaxed);
                continue;
            }
            if (dequeue_pos_.compare_exchange_weak(
                    position, position + count,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;
            }
        }

        for (std::size_t offset = 0; offset < count; ++offset) {
            Cell &cell = cells_[(position + offset) & (Capacity - 1)];
            values[offset] = std::move(*cell.object());
            cell.object()->~T();
            cell.sequence.store(position + offset + Capacity,
                                std::memory_order_release);
        }
        return true;
    }

    // Wait until an item is available. Use try_pop() when polling is desired.
    void pop(T &value) noexcept(std::is_nothrow_move_assignable<T>::value) {
        while (!try_pop(value)) {
            std::this_thread::yield();
        }
    }

    bool empty() const noexcept {
        const std::uint64_t position =
            dequeue_pos_.load(std::memory_order_relaxed);
        return cells_[position & (Capacity - 1)].sequence.load(
                   std::memory_order_acquire) < position + 1;
    }

    bool full() const noexcept {
        const std::uint64_t enqueue =
            enqueue_pos_.load(std::memory_order_acquire);
        const std::uint64_t dequeue =
            dequeue_pos_.load(std::memory_order_acquire);
        return enqueue - dequeue >= Capacity;
    }

private:
    alignas(64) std::atomic<std::uint64_t> enqueue_pos_{0};
    alignas(64) std::atomic<std::uint64_t> dequeue_pos_{0};
    Cell cells_[Capacity]{};
};

#endif  // BASIC_MPMC_QUEUE_H_

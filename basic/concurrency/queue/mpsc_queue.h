#ifndef BASIC_MPSC_QUEUE_H_
#define BASIC_MPSC_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// A bounded multi-producer, single-consumer queue.
//
// Producers may call try_push()/try_emplace() concurrently. Only one
// consumer may call try_pop(). The queue never allocates after construction.
// A failed producer operation does not modify its input value.
template <typename T, std::size_t Capacity>
class MpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two and at least two");
    static_assert(Capacity <= (std::size_t{1} << 63),
                  "Capacity must fit in a signed 64-bit sequence distance");

    struct Cell {
        std::atomic<std::uint64_t> sequence{0};
        typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
        std::atomic<bool> constructed{false};

        T *object() noexcept {
            return std::launder(reinterpret_cast<T *>(&storage));
        }
    };

public:
    MpscQueue() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MpscQueue() noexcept {
        // The queue must be quiescent before destruction. This also cleans up
        // an item that was pushed but not yet consumed.
        for (Cell &cell : cells_) {
            if (cell.constructed.exchange(false, std::memory_order_relaxed)) {
                cell.object()->~T();
            }
        }
    }

    MpscQueue(const MpscQueue &) = delete;
    MpscQueue &operator=(const MpscQueue &) = delete;
    MpscQueue(MpscQueue &&) = delete;
    MpscQueue &operator=(MpscQueue &&) = delete;

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
                      "MpscQueue requires a noexcept element constructor");

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
        cell->constructed.store(true, std::memory_order_relaxed);
        cell->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    // Call from the single consumer thread only.
    bool try_pop(T &value) noexcept(std::is_nothrow_move_assignable<T>::value) {
        static_assert(std::is_nothrow_move_assignable<T>::value,
                      "MpscQueue requires a noexcept move assignment");

        const std::uint64_t position =
            dequeue_pos_.load(std::memory_order_relaxed);
        Cell &cell = cells_[position & (Capacity - 1)];
        const std::uint64_t sequence =
            cell.sequence.load(std::memory_order_acquire);
        const std::int64_t difference = static_cast<std::int64_t>(
            sequence - (position + 1));

        if (difference < 0) {
            return false;
        }

        value = std::move(*cell.object());
        cell.object()->~T();
        cell.constructed.store(false, std::memory_order_relaxed);
        cell.sequence.store(position + Capacity,
                            std::memory_order_release);
        dequeue_pos_.store(position + 1, std::memory_order_relaxed);
        return true;
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

#endif  // BASIC_MPSC_QUEUE_H_

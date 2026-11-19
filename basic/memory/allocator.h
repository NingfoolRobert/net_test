/**
 * @file alloctor.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-06-12
 */
#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <vector>

namespace basic {
template <typename T, std::size_t N = 2048>
class Allocator {
public:
    Allocator() : free_slots_(N), storage_(std::allocator<T>{}.allocate(N)) {
        for (std::size_t i = 0; i < N; ++i) {
            free_slots_[i] = storage_ + i;
        }
    }
    // Copying the free list would create two owners of the same storage.
    Allocator(const Allocator &) = delete;
    Allocator &operator=(const Allocator &) = delete;
    ~Allocator() {
        std::allocator<T>{}.deallocate(storage_, N);
    }

public:
    template <typename ...Args>
    T *allocate(const Args &&...args) {
        if (free_count_ == 0) {
            return nullptr;
        }
        T *slot = free_slots_[head_];
        head_ = (head_ + 1) % free_slots_.size();
        --free_count_;
        try {
            return ::new (static_cast<void *>(slot)) T(args...);
        } catch (...) {
            // Restore the reservation at the front, even if T reentered this pool.
            head_ = head_ == 0 ? free_slots_.size() - 1 : head_ - 1;
            free_slots_[head_] = slot;
            ++free_count_;
            throw;
        }
    }

    void deallocate(T *p) {
        if (nullptr == p) {
            return;
        }
        p->~T();
        free_slots_[(head_ + free_count_) % free_slots_.size()] = p;
        ++free_count_;
    }

private:
    std::vector<T *> free_slots_;
    T *storage_;
    std::size_t head_ = 0;
    std::size_t free_count_ = N;
};
}  // namespace basic

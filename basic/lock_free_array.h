/**
 * @file lock_free_array.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-05
 */

#ifndef _LOCK_FREE_ARRAY_H_
#define _LOCK_FREE_ARRAY_H_ 1

#include <atomic>
#include <immintrin.h>
#include <stdio.h>

#ifdef __GNUC__
#include <pthread.h>
#define CAS(ptr, old_val, new_val) __sync_bool_compare_and_swap(ptr, old_val, new_val)
#else
#define CAS(ptr, old_val, new_val) atomic_compare_exchange_weak(ptr, old_val, new_val)
#endif
template <typename TYPE, size_t N = 1024>
class lock_free_array {

    size_t write_index = 0;
    size_t read_index = 0;
    size_t max_num_read_index = 0;
    TYPE queue_[N];

public:
    static_assert((N & N - 1) && N, "the size must be a power of 2 !!!");
    lock_free_array() {
    }
    ~lock_free_array() {
    }

    lock_free_array(const lock_free_array<TYPE, N> &arr) = delete;
    lock_free_array<TYPE, Size> operator=(lock_free_array<TYPE, N> &rhs) = delete;

public:
    size_t index(size_t count) {
        return count & (N - 1);
    }
    /**
     * @brief Push an element into the buffer
     * @param data The element to push
     * @return true if the element was pushed, false otherwise
     */
    bool push(const TYPE &data) {
        size_t current_read_index;
        size_t current_write_index;

        do {
            current_write_index = write_index;
            current_read_index = read_index;
            if (index(current_write_index + 1) == index(current_read_index))
                return false;
        } while (!CAS(&write_index, current_write_index, (current_write_index + 1)));

        queue_[index(current_write_index)] = data;

        while (!CAS(&max_num_read_index, current_write_index, (current_write_index + 1))) {
            // sched_yield();
            _mm_pause();
        }

        return true;
    }

    //    TYPE pop() {
    //        size_t current_read_index = 0;
    //        size_t current_max_num_read_index = 0;
    //        TYPE data;
    //        do {
    //            current_read_index = read_index;
    //            current_max_num_read_index = max_num_read_index;
    //
    //            if (index(current_read_index) == index(current_max_num_read_index))
    //                return TYPE{};
    //
    //            data = queue_[index(current_read_index)];
    //
    //            if (CAS(&read_index, current_read_index, current_read_index + 1)) {
    //                return data;
    //            }
    //
    //        } while (true);
    //
    //        return TYPE{};
    //    }

    bool pop(TYPE &data) {
        while (!try_pop(data)) {
            _mm_pause();
        }
        return true;
    }
    /**
     * @brief Try to pop an element from the buffer
     * @param data Reference to store the popped element
     * @return true if an element was popped, false otherwise
     */
    bool try_pop(TYPE &data) {

        if (empty()) {
            return false;
        }
        //
        size_t current_read_index = 0;
        current_read_index = read_index;
        data = queue_[index(current_read_index)];
        return CAS(&read_index, current_read_index, current_read_index + 1);
    }
    /**
     * @brief Check if the buffer is empty
     * @return true if the buffer is empty, false otherwise
     */
    bool empty() {
        return read_index == write_index;
    }
    /**
     * @brief Get the size of the buffer
     * @return size_t size of the buffer
     */
    size_t size() {
        return ((write_index - read_index) + N) & (N - 1);
    }
    /**
     * @brief Check if the buffer is full
     * @return true if the buffer is full, false otherwise
     */
    bool full() {
        return (write_index + 1) % size() == read_index;
    }
};

#endif

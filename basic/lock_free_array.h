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
#include <stdio.h>

#ifdef __GNUC__
#include <pthread.h>
#define CAS(ptr, old_val, new_val) __sync_bool_compare_and_swap(ptr, old_val, new_val)
#else
#define CAS(ptr, old_val, new_val) atomic_compare_exchange_weak(ptr, old_val, new_val)
#endif
template <typename TYPE, size_t Size = 1024>
class lock_free_array {

    size_t write_index = 0;
    size_t read_index = 0;
    size_t max_num_read_index = 0;
    TYPE queue_[Size];

public:
    static_assert((Size & Size - 1) && Size, "the size must be a power of 2 !!!");
    lock_free_array() {
    }
    ~lock_free_array() {
    }

    lock_free_array(const lock_free_array<TYPE, Size> &arr) = delete;
    lock_free_array<TYPE, Size> operator=(lock_free_array<TYPE, Size> &rhs) = delete;

public:
    size_t index(size_t count) {
        return count & (Size - 1);
    }

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
            sched_yield();
        }

        return true;
    }

    TYPE pop() {
        size_t current_read_index = 0;
        size_t current_max_num_read_index = 0;
        TYPE data;
        do {
            current_read_index = read_index;
            current_max_num_read_index = max_num_read_index;

            if (index(current_read_index) == index(current_max_num_read_index))
                return TYPE{};

            data = queue_[index(current_read_index)];

            if (CAS(&read_index, current_read_index, current_read_index + 1)) {
                return data;
            }

        } while (true);

        return TYPE{};
    }

    bool empty() {
        return read_index == write_index;
    }

    size_t size() {
        return ((write_index - read_index) + Size) & (Size - 1);
    }
};

#endif

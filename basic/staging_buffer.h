/**
 * @file staging_buffer.h
 * @brief

 * @author bfning
 * @version 0.1
 * @date 2025-01-12
 */
#pragma once
#include <cstdio>

#include "memory_fence.h"

namespace detail {

template <size_t N>
class StagingBuffer {
public:
    /**
     * @brief FIFO Bytes Buffer
     */
    StagingBuffer()
        : min_free_space_(N), consumer_pos_(storage_), producer_pos_(storage_), end_of_record_(storage_ + N) {
    }
    //
    ~StagingBuffer() = default;
    /**
     * @brief
     *
     * @param size
     * @param blocking
     *
     * @return
     */
    //
    char *allocate(size_t size, bool blocking = true) {
        const char *end_of_buffer = storage_ + N;

        while (size >= min_free_space_) {
            char *cached_consumer_pos = consumer_pos_;

            if (cached_consumer_pos <= producer_pos_) {
                min_free_space_ = end_of_buffer - producer_pos_;

                if (min_free_space_ > size) {
                    break;
                }
                // not enough space at the end of the buffer; wrap around
                end_of_record_ = producer_pos_;
                //
                if (cached_consumer_pos != storage_) {
                    // prevents producer  from updating before end_of_buffer
                    MemoryFence::sfence();
                    producer_pos_ = storage_;
                    min_free_space_ = cached_consumer_pos - producer_pos_;
                }
            }
            else {
                min_free_space_ = cached_consumer_pos - producer_pos_;
            }

            // need  to prevent  infinite loops in tests
            if (!blocking && size >= min_free_space_) {
                return nullptr;
            }
            // check current consumer position and clear position
            size_t  wait_consumer_position = 0;
            peek(&wait_consumer_position);
            //
        }
        //
        MemoryFence::sfence();
        auto ptr = producer_pos_;
        min_free_space_ -= size;
        producer_pos_ += size;
        ++num_allocates_;
        return ptr;
    }

    /**
     * @brief
     *
     * @param size
     */
    void deallocate(size_t size) {
        // make sure consumer reads finish  befor bump
        MemoryFence::lfence();
        consumer_pos_ += size;
    }

private:
    char *peek(size_t *byte_available) {
        char *cached_producer_pos = producer_pos_;
        if (cached_producer_pos < consumer_pos_) {
            MemoryFence::lfence();
            *byte_available = end_of_record_ - consumer_pos_;
            if (*byte_available > 0) {
                return consumer_pos_;
            }
            // roll over
            consumer_pos_ = storage_;
        }
        *byte_available = cached_producer_pos - consumer_pos_;
        return consumer_pos_;
    }

private:
    char storage_[N];

    char *producer_pos_;
    char *consumer_pos_;
    char *end_of_record_;
    size_t min_free_space_;

    size_t num_allocates_;
};
}  // namespace detail

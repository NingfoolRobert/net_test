/**
 * @file fixed_buffer.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-18
 */
#pragma once
#include <stdio.h>
#include <stdlib.h>

namespace detail {

template <size_t N>
class FixedBuffer {
public:
    FixedBuffer() : head_(0), tail_(0) {
        data_[0] = 0;
    }
    ~FixedBuffer() = default;

    FixedBuffer(const FixedBuffer &rhs) {
        head_ = rhs.head_;
        tail_ = rhs.tail_;
        memcpy(data_, rhs.data_, N);
    }

    size_t capacity() {
        return N;
    }

    size_t length() {
        return tail_ - head_;
    }

    char *data() {
        return data_;
    }

    const char *data() const {
        return data_;
    }

    char *peek(size_t off) {
        if (head_ + off > tail_) {
            return nullptr;
        }
        head_ += off;
        return data_ + head_;
    }

    void shrink_to_fit() {
        memove(data_, data(), length());
        tail_ -= head_;
        head_ = 0;
    }

    bool empty() {
        return head_ == tail_;
    }

    size_t remaining() {
        return N - tail_;
    }

    bool append(const char *data, size_t len) {
        if (nullptr == data || remaining() < len) {
            return false;
        }
        //
        memcpy(data_ + tail_, data, len);
        tail_ += len;
        return true;
    }

private:
    size_t head_;
    size_t tail_;

    char data_[N];
};


template <size_t N>
using fixed_buffer_t = FixedBuffer<N>;
}  // namespace detail

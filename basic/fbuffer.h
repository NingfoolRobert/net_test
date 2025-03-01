/**
 * @file fbuffer.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-18
 */
#pragma once
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace detail {

template <size_t N>
class fbuffer {
public:
    fbuffer() : head_(0), tail_(0) {
        data_[0] = 0;
    }
    ~fbuffer() = default;

    fbuffer(const fbuffer &rhs) {
        head_ = rhs.head_;
        tail_ = rhs.tail_;
        memcpy(data_, rhs.data_, N);
    }

    size_t capacity() const {
        return N;
    }

    size_t length() const {
        return tail_ - head_;
    }

    char *data() {
        return data_;
    }

    const char *data() const {
        return data_;
    }

    char *peak(size_t off) {
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

    bool empty() const {
        return head_ == tail_;
    }

    bool append(const void *data, size_t len) {
        if (nullptr == data || N - tail_ < len) {
            return false;
        }
        //
        memcpy(data_ + tail_, data, len);
        tail_ += len;
        return true;
    }

    void add_data_len(size_t len) {
        tail_ += len;
    }

    std::string hex() const {
        std::stringstream ss;
        auto p = data();
        size_t len = length();
        for (auto i = 0u; i < len; ++i) {
            ss << std::hex << static_cast<const unsigned int>(p[i]) << " ";
            if (i && i % 16 == 0) {
                ss << "\n";
            }
        }
        //
        if (len % 16) {
            ss << "\n";
        }
        return ss.str();
    }

private:
    size_t head_;
    size_t tail_;

    char data_[N];
};

template <size_t N>
std::ostream &operator<<(std::ostream &out, const fbuffer<N> &rhs) {
    out << rhs.hex();
    return out;
}

}  // namespace detail

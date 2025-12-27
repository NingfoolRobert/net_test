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
        if (tail_ >= head_) {
            return tail_ - head_;
        }
        else {
            return (N + 1) - head_ + tail_;
        }
    }

    size_t size() const {
        return length();
    }

    char *data() {
        return data_;
    }

    const char *data() const {
        return data_;
    }
    /**
     * @brief Move the head pointer forward by the specified offset and return the pointer to the new head position.
     * @param off The offset to move the head pointer.
     * @return A pointer to the new head position after moving the offset. Returns nullptr if
     */
    char *peak(size_t off) {
        if (head_ + off > tail_) {
            return nullptr;
        }
        head_ += off;
        return data_ + head_;
    }
    /**
     * @brief Shrink the buffer to fit the current data by moving the data to the beginning of the buffer.
     */
    void shrink_to_fit() {
        memmove(data_, data(), length());
        tail_ -= head_;
        head_ = 0;
    }

    bool empty() const {
        return head_ == tail_;
    }

    /**<brief> Append data to the buffer.
     * @param data Pointer to the data to append.
     * @param len Length of the data to append.
     * @return true if the data was successfully appended; false otherwise.
     */
    bool append(const void *data, size_t len) {
        if (nullptr == data || N - tail_ < len) {
            return false;
        }
        //
        memcpy(data_ + tail_, data, len);
        tail_ += len;
        return true;
    }

    const char operator[](size_t index) const {
        return data_[(head_ + index) % N];
    }

    char &operator[](size_t index) {
        return data_[(head_ + index) % N];
    }
    //
    size_t contious_size() const {
        return N - tail_;
    }

private:
    char data_[N];
    //
    size_t head_;  // read position
    size_t tail_;  // write position
};

template <size_t N>
inline std::ostream &operator<<(std::ostream &ss, const fbuffer<N> &rhs) {
    for (auto i = 0u; i < 16; ++i) {
        if (i && i % 8 == 0) {
            ss << "  ";
        }
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned int>(i) << " ";
    }
    ss << "\n-------------------------------------------------" << std::endl;
    for (auto i = 0u; i < rhs.length(); ++i) {
        if (i && i % 16 == 0) {
            ss << "\n";
        }
        if (i && i % 8 == 0) {
            ss << "  ";
        }
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned int>(rhs[i]) << " ";
    }
    ss << std::endl;
    return ss;
}

template <size_t N>
using fbuffer_t = fbuffer<N>;
}  // namespace detail

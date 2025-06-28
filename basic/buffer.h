/**
 * @file buffer.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-09
 */
#ifndef _BUFFER_H_
#define _BUFFER_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

namespace detail {
class buffer_t {

public:
    using size_type = unsigned int;
    using pointer = char *;
    using const_pointer = const char *;

    buffer_t() : cap_(0), size_(0), data_offset_(0), data_(nullptr) {
    }

    buffer_t(const buffer_t &rhs) {
        cap_ = rhs.cap_;
        data_ = (char *)malloc(rhs.cap_);
        memcpy(data_, rhs.data_, rhs.size_);
        size_ = rhs.size_;
    }

    buffer_t(buffer_t &&rhs) noexcept {
        cap_ = rhs.cap_;
        rhs.cap_ = 0;
        size_ = rhs.size_;
        rhs.size_ = 0;
        data_ = rhs.data_;
        rhs.data_ = nullptr;
    }

    buffer_t &operator=(const buffer_t &rhs) {
        size_ = rhs.size_;
        cap_ = rhs.cap_;
        data_ = reinterpret_cast<pointer>(malloc(cap_));
        memcpy(data_, rhs.data_, size_);
        return *this;
    }

    bool append(const void *data, size_type len) {
        //
        if (len > cap_ - size_) {
            return false;
        }
        //
        memcpy(data_ + size_, data, len);
       size_ += len;
        return true;
    }

    size_type resize(size_type len) {
        if(len  > cap_ - size_) {
            return cap_;
        }
        //
        return cap_;
    }

    pointer peak(size_type offset) {
        return data_ + offset;
    }

    pointer buf() {
        return data_;
    }

    const_pointer buf() const {
        return data_;
    }

    pointer data() {
        return data_ + data_offset_;
    }

    const_pointer data() const {
        return data_ + data_offset_;
    }

    size_type data_size() {
        return size_ - data_offset_;
    }

    template <typename T>
    bool copy(T &data) {
        static_assert(std::is_trivially_copyable_v<T>, "don't support the sizeof(T) obj");
        return append(data, sizeof(data));
    }

private:
    size_type cap_{0};
    size_type size_{0};
    size_type data_offset_{0};
    pointer data_{nullptr};
};
}  // namespace detail

#endif

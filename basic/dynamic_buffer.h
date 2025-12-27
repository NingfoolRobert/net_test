/**
 * @file dynamic_buffer.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-12-27
 */
#pragma once
#include <cstddef>
#include <cstring>
#include <string>

namespace detail {
class DynamicBufferr {
public:
    DynamicBufferr() {
    }

    ~DynamicBufferr() {
        clear(true);
    }

    DynamicBufferr(const DynamicBufferr &rhs) {
        capacity_ = rhs.capacity_;
        size_ = rhs.size_;
        head_ = rhs.head_;
        data_ = new char[capacity_];
        memcpy(data_, rhs.data_, size_);
    }
    DynamicBufferr(DynamicBufferr &&rhs) noexcept {
        capacity_ = rhs.capacity_;
        rhs.capacity_ = 0;
        size_ = rhs.size_;
        rhs.size_ = 0;
        head_ = rhs.head_;
        data_ = rhs.data_;
        rhs.data_ = nullptr;
    }
    DynamicBufferr &operator=(const DynamicBufferr &rhs) {
        size_ = rhs.size_;
        capacity_ = rhs.capacity_;
        head_ = rhs.head_;
        data_ = new char[capacity_];
        memcpy(data_, rhs.data_, size_);
        return *this;
    }

public:
    size_t capacity() const {
        return capacity_;
    }

    size_t size() const {
        return size_;
    }

    void set_head_size(size_t head) {
        head_ = head;
    }

    size_t head_size() const {
        return head_;
    }

    void set_expand_size(size_t size) {
        expand_size_ = size;
    }

    size_t expand_size() const {
        return expand_size_;
    }

    char *data() {
        return data_ + head_;
    }

    const char *data() const {
        return data_ + head_;
    }

    char *buffer_ptr() {
        return data_;
    }

    const char *buffer_ptr() const {
        return data_;
    }

    void reserve(size_t size) {
        if (size <= capacity_) {
            return;
        }
        //
        size_t allocate_size = capacity_;
        while (allocate_size < size) {
            allocate_size += expand_size_;
        }
        //
        char *new_data = new char[allocate_size];
        if (data_) {
            memcpy(new_data, data_, size_);
            delete[] data_;
        }
        //
        data_ = new_data;
        capacity_ = allocate_size;
        data_[capacity_ - 1] = 0;
    }

    void resize(size_t size) {
        reserve(size);
        size_ = size;
    }

    void append(const char *data, size_t size) {
        reserve(size_ + size);
        memcpy(data_ + size_, data, size);
        size_ += size;
    }

    void clear(bool reset_capacity = false) {
        size_ = 0;
        head_ = 0;
        if (reset_capacity && data_) {
            capacity_ = 0;
            delete[] data_;
            data_ = nullptr;
        }
    }

    const char operator[](size_t index) const {
        return data_[head_ + index];
    }

    char &operator[](size_t index) {
        return data_[head_ + index];
    }
    //
    const char *c_str() const {
        return buffer_ptr();
    }
    //
    std::string to_string() const {
        return std::string(data(), size());
    }

    template <typename T>
    const T *as() const {
        if (sizeof(T) > size()) {
            return nullptr;
        }
        return reinterpret_cast<const T *>(data());
    }
    //
    template <typename T>
    T *as() {
        if (sizeof(T) > size()) {
            return nullptr;
        }
        return reinterpret_cast<T *>(data());
    }

private:
    size_t capacity_;
    size_t size_ = {0};
    size_t head_ = {0};
    char *data_{nullptr};

private:
    size_t expand_size_ = 256;
};


using dynamic_buffer_t = DynamicBufferr;
}  // namespace detail

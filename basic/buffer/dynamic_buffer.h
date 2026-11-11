/**
 * @file dynamic_buffer.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-12-27
 */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace detail {
class DynamicBufferr {
public:
    DynamicBufferr() = default;

    DynamicBufferr(const DynamicBufferr &rhs)
        : capacity_(rhs.capacity_), size_(rhs.size_), head_(rhs.head_),
          data_(capacity_ ? new char[capacity_] : nullptr), expand_size_(rhs.expand_size_) {
        if (size_ != 0) {
            std::memcpy(data_.get(), rhs.data_.get(), size_);
        }
    }

    DynamicBufferr(DynamicBufferr &&rhs) noexcept
        : capacity_(std::exchange(rhs.capacity_, 0)), size_(std::exchange(rhs.size_, 0)),
          head_(std::exchange(rhs.head_, 0)), data_(std::move(rhs.data_)),
          expand_size_(rhs.expand_size_) {}

    DynamicBufferr &operator=(const DynamicBufferr &rhs) {
        if (this != &rhs) {
            DynamicBufferr copy(rhs);
            using std::swap;
            swap(capacity_, copy.capacity_);
            swap(size_, copy.size_);
            swap(head_, copy.head_);
            swap(data_, copy.data_);
            swap(expand_size_, copy.expand_size_);
        }
        return *this;
    }

    [[nodiscard]] size_t capacity() const { return capacity_; }
    [[nodiscard]] size_t size() const { return size_; }
    [[nodiscard]] size_t readable_size() const { return size_ - head_; }

    void set_head_size(size_t head) {
        if (head > size_) {
            throw std::out_of_range("buffer head exceeds size");
        }
        head_ = head;
    }

    [[nodiscard]] size_t head_size() const { return head_; }

    void set_expand_size(size_t size) {
        if (size == 0) {
            throw std::invalid_argument("buffer expansion step must be positive");
        }
        expand_size_ = size;
    }

    [[nodiscard]] size_t expand_size() const { return expand_size_; }

    char *data() { return data_ ? data_.get() + head_ : nullptr; }
    const char *data() const { return data_ ? data_.get() + head_ : nullptr; }
    char *buffer_ptr() { return data_.get(); }
    const char *buffer_ptr() const { return data_.get(); }

    void reserve(size_t size) {
        if (size > capacity_) {
            grow(size, nullptr, 0);
        }
    }

    void resize(size_t size) {
        reserve(size);
        size_ = size;
        head_ = std::min(head_, size_);
    }

    void append(const char *data, size_t size) {
        if (size == 0) {
            return;
        }
        if (data == nullptr) {
            throw std::invalid_argument("null buffer source");
        }
        if (size > std::numeric_limits<size_t>::max() - size_) {
            throw std::length_error("buffer size overflow");
        }
        const auto requested = size_ + size;
        if (requested > capacity_) {
            // Keep the old storage alive until the appended bytes are copied.
            grow(requested, data, size);
        } else {
            std::memmove(data_.get() + size_, data, size);
        }
        size_ = requested;
    }

    void clear(bool reset_capacity = false) {
        size_ = 0;
        head_ = 0;
        if (reset_capacity) {
            capacity_ = 0;
            data_.reset();
        }
    }

    char operator[](size_t index) const { return data_[head_ + index]; }
    char &operator[](size_t index) { return data_[head_ + index]; }

    [[nodiscard]] const char *c_str() const { return buffer_ptr(); }

    [[nodiscard]] std::string to_string() const {
        if (readable_size() == 0) {
            return {};
        }
        return std::string(data(), readable_size());
    }

    // Copy a valid native object representation at the current head. The
    // caller owns the destination object and validates any external encoding.
    template <typename T>
    bool read(T &out) const {
        static_assert(std::is_trivially_copyable_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>,
                      "read requires a writable trivially copyable object");
        if (sizeof(T) > readable_size()) {
            return false;
        }
        std::memmove(std::addressof(out), data(), sizeof(T));
        return true;
    }

private:
    void grow(size_t requested, const char *extra, size_t extra_size) {
        const auto limit = static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max());
        const auto delta = requested - capacity_;
        const auto steps = delta / expand_size_ + (delta % expand_size_ != 0);
        if (steps > (limit - capacity_) / expand_size_) {
            throw std::length_error("buffer capacity overflow");
        }
        const auto capacity = capacity_ + steps * expand_size_;
        std::unique_ptr<char[]> storage(new char[capacity]);
        storage[capacity - 1] = 0;
        if (size_ != 0) {
            std::memcpy(storage.get(), data_.get(), size_);
        }
        if (extra_size != 0) {
            std::memcpy(storage.get() + size_, extra, extra_size);
        }
        data_ = std::move(storage);
        capacity_ = capacity;
    }

    size_t capacity_ = 0;
    size_t size_ = 0;
    size_t head_ = 0;
    std::unique_ptr<char[]> data_;
    size_t expand_size_ = 256;
};

using dynamic_buffer_t = DynamicBufferr;
}  // namespace detail

/**
 * @file buffer.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-09
 */
#pragma once

#include <stdlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace detail {
class buffer_t {
public:
    using size_type     = unsigned int;
    using pointer       = char *;
    using const_pointer = const char *;

    buffer_t() = default;

    explicit buffer_t(size_type capacity) : cap_(capacity), data_(Allocate(capacity)) {
    }

    buffer_t(const buffer_t &rhs)
        : cap_(rhs.cap_), size_(rhs.size_), data_offset_(rhs.data_offset_), data_(Allocate(rhs.cap_)) {
        if (size_ != 0) {
            std::memcpy(data_.get(), rhs.data_.get(), size_);
        }
    }

    buffer_t(buffer_t &&rhs) noexcept
        : cap_(rhs.cap_), size_(rhs.size_), data_offset_(rhs.data_offset_), data_(std::move(rhs.data_)) {
        rhs.cap_         = 0;
        rhs.size_        = 0;
        rhs.data_offset_ = 0;
    }

    buffer_t &operator=(const buffer_t &rhs) {
        if (this != &rhs) {
            buffer_t copy(rhs);
            *this = std::move(copy);
        }
        return *this;
    }

    buffer_t &operator=(buffer_t &&rhs) noexcept {
        if (this != &rhs) {
            cap_             = rhs.cap_;
            size_            = rhs.size_;
            data_offset_     = rhs.data_offset_;
            data_            = std::move(rhs.data_);
            rhs.cap_         = 0;
            rhs.size_        = 0;
            rhs.data_offset_ = 0;
        }
        return *this;
    }
    ~buffer_t() = default;

    bool append(const void *data, size_type len) {
        if (len == 0) {
            return true;
        }
        if (data == nullptr || len > cap_ - size_) {
            return false;
        }
        std::memmove(data_.get() + size_, data, len);
        size_ += len;
        return true;
    }

    // Change capacity, preserving the prefix that fits. Shrinking truncates data.
    size_type resize(size_type len) {
        if (len == cap_) {
            return cap_;
        }
        auto replacement = Allocate(len);
        auto kept        = std::min(size_, len);
        if (kept != 0) {
            std::memcpy(replacement.get(), data_.get(), kept);
        }
        data_        = std::move(replacement);
        cap_         = len;
        size_        = kept;
        data_offset_ = std::min(data_offset_, size_);
        return cap_;
    }

    // Grow storage in fixed steps, preserving existing data and offsets.
    void reset(size_type capacity) {
        if (capacity <= cap_) {
            return;
        }
        const size_type remainder = capacity % kAllocationStep;
        if (remainder != 0) {
            const size_type padding = kAllocationStep - remainder;
            if (capacity > std::numeric_limits<size_type>::max() - padding) {
                throw std::length_error("buffer capacity exceeds size_type");
            }
            capacity += padding;
        }
        resize(capacity);
    }

    [[nodiscard]] size_type capacity() const noexcept {
        return cap_;
    }
    [[nodiscard]] size_type size() const noexcept {
        return size_;
    }
    void clear() noexcept {
        size_ = data_offset_ = 0;
    }

    // Address allocated storage; offsets at or beyond capacity are invalid.
    pointer peak(size_type offset) {
        return data_ && offset < cap_ ? data_.get() + offset : nullptr;
    }

    pointer buf() {
        return data_.get();
    }

    [[nodiscard]] const_pointer buf() const {
        return data_.get();
    }

    pointer data() {
        return data_ ? data_.get() + data_offset_ : nullptr;
    }

    [[nodiscard]] const_pointer data() const {
        return data_ ? data_.get() + data_offset_ : nullptr;
    }

    [[nodiscard]] size_type data_size() const {
        return size_ - data_offset_;
    }

    template <typename T>
    bool copy(const T &data) {
        static_assert(std::is_trivially_copyable_v<T>, "buffer_t::copy requires a trivially copyable type");
        return append(&data, sizeof(data));
    }

private:
    struct FreeDeleter {
        // This RAII deleter must pair posix_memalign storage with free.
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
        void operator()(pointer data) const noexcept {
            std::free(data);
        }
    };
    using Storage                                = std::unique_ptr<char[], FreeDeleter>;
    static constexpr std::size_t kAlignment      = 64;
    static constexpr size_type   kAllocationStep = 256;

    static Storage Allocate(size_type capacity) {
        if (capacity == 0) {
            return Storage{};
        }
        void *data = nullptr;
        if (::posix_memalign(&data, kAlignment, capacity) != 0) {
            throw std::bad_alloc();
        }
        // Keep newly allocated storage zero-initialized, including unused capacity.
        std::memset(data, 0, capacity);
        return Storage(static_cast<pointer>(data));
    }

    size_type cap_{0};
    size_type size_{0};
    size_type data_offset_{0};
    Storage   data_{nullptr};
};
}  // namespace detail

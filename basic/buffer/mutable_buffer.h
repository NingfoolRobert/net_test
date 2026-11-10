/**
 * @file mutable_buffer.h
 * @brief Non-owning, bounded byte buffer with append and consume operations.
 * @author bfning
 * @version 0.2
 * @date 2026-09-13
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <limits>

namespace basic {

// The caller owns the backing storage and must keep it alive for this object's
// lifetime. The buffer stores bytes in [0, size()) and never grows its storage.
class mutable_buffer_t {
 public:
  mutable_buffer_t(char *data, std::size_t capacity) noexcept
      : data_(data), capacity_(data == nullptr ? 0 : capacity) {}

  // Append all bytes or leave the buffer unchanged. A zero-length append is
  // valid and does not require a non-null source pointer.
  bool try_append(const void *data, std::size_t len) noexcept {
    if (len == 0) {
      return true;
    }
    if (data == nullptr || len > remaining()) {
      return false;
    }

    std::memmove(data_ + size_, data, len);
    size_ += len;
    return true;
  }

  // Kept for source compatibility: returns the appended byte count, or zero
  // if the append fails. Use try_append() when zero-length appends matter.
  int append(const void *data, std::size_t len) noexcept {
    if (len > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !try_append(data, len)) {
      return 0;
    }
    return static_cast<int>(len);
  }

  // Remove len bytes from the front while preserving the remaining bytes.
  // Returns false and leaves the buffer unchanged if len exceeds size().
  bool consume(std::size_t len) noexcept {
    if (len > size_) {
      return false;
    }
    const std::size_t remaining_size = size_ - len;
    if (remaining_size != 0 && len != 0) {
      std::memmove(data_, data_ + len, remaining_size);
    }
    size_ = remaining_size;
    return true;
  }

  // Legacy API: historically this consumed bytes despite its name. Preserve
  // that behavior; invalid lengths clear the buffer as before.
  void peek(std::size_t len) noexcept {
    if (!consume(len)) {
      clear();
    }
  }

  void clear() noexcept { size_ = 0; }

  std::size_t size() const noexcept { return size_; }
  std::size_t cap() const noexcept { return capacity_; }
  std::size_t capacity() const noexcept { return capacity_; }
  std::size_t remaining() const noexcept { return capacity_ - size_; }
  bool empty() const noexcept { return size_ == 0; }

  const char *data() const noexcept { return data_; }
  char *mutable_data() noexcept { return data_; }

 private:
  char *data_;
  std::size_t capacity_;
  std::size_t size_{};
};

}  // namespace basic

/**
 * @file fixed_vector.h
 * @brief Dynamically growing segmented vector with stable element addresses.
 * @author bfning
 * @version 0.3
 * @date 2026-09-13
 */
#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace basic {

template <typename T, std::size_t InitSize = 1024, std::size_t ExpandSize = 1024>
class FixedVector {
  static_assert(ExpandSize > 0, "ExpandSize must be greater than zero");

  using allocator_type = std::allocator<T>;
  using allocator_traits = std::allocator_traits<allocator_type>;

 public:
  FixedVector() {
    try {
      reserve(InitSize);
    } catch (...) {
      release_storage();
      throw;
    }
  }

  ~FixedVector() { release_storage(); }

  FixedVector(const FixedVector &) = delete;
  FixedVector &operator=(const FixedVector &) = delete;

  FixedVector(FixedVector &&other) noexcept { take_storage(other); }

  FixedVector &operator=(FixedVector &&other) noexcept {
    if (this != &other) {
      release_storage();
      take_storage(other);
    }
    return *this;
  }

  // Unchecked access; idx must be less than size().
  T &operator[](std::size_t idx) noexcept {
    return chunks_[idx / ExpandSize][idx % ExpandSize];
  }

  const T &operator[](std::size_t idx) const noexcept {
    return chunks_[idx / ExpandSize][idx % ExpandSize];
  }

  T &at(std::size_t idx) {
    if (idx >= size_) {
      throw std::out_of_range("FixedVector index out of range");
    }
    return (*this)[idx];
  }

  const T &at(std::size_t idx) const {
    if (idx >= size_) {
      throw std::out_of_range("FixedVector index out of range");
    }
    return (*this)[idx];
  }

  T &front() { return at(0); }
  const T &front() const { return at(0); }
  T &back() { return at(size_ - 1); }
  const T &back() const { return at(size_ - 1); }

  std::size_t size() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return chunk_count_ * ExpandSize; }
  bool empty() const noexcept { return size_ == 0; }

  // Reserve whole chunks. Existing elements never move.
  void reserve(std::size_t requested_capacity) {
    const std::size_t required_chunks =
        requested_capacity / ExpandSize + (requested_capacity % ExpandSize != 0);
    if (required_chunks > std::numeric_limits<std::size_t>::max() / ExpandSize) {
      throw std::length_error("FixedVector capacity overflow");
    }

    grow_chunk_table(required_chunks);
    while (chunk_count_ < required_chunks) {
      chunks_[chunk_count_] = allocator_traits::allocate(allocator_, ExpandSize);
      ++chunk_count_;
    }
  }

  template <typename... Args>
  T &emplace_back(Args &&...args) {
    if (size_ == capacity()) {
      if (size_ == std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("FixedVector capacity overflow");
      }
      // One fixed-size data chunk per expansion; the pointer table grows
      // geometrically, so metadata reallocations are amortized.
      reserve(size_ + 1);
    }

    T *element = chunks_[size_ / ExpandSize] + (size_ % ExpandSize);
    allocator_traits::construct(allocator_, element, std::forward<Args>(args)...);
    ++size_;
    return *element;
  }

  T &push_back(const T &value) { return emplace_back(value); }
  T &push_back(T &&value) { return emplace_back(std::move(value)); }

  void pop_back() {
    if (empty()) {
      throw std::out_of_range("FixedVector::pop_back on empty container");
    }
    allocator_traits::destroy(allocator_, chunks_[(size_ - 1) / ExpandSize] +
                                              ((size_ - 1) % ExpandSize));
    --size_;
  }

  // Destroy all elements while retaining allocated chunks for reuse.
  void clear() noexcept {
    while (size_ > 0) {
      --size_;
      allocator_traits::destroy(allocator_, chunks_[size_ / ExpandSize] +
                                                (size_ % ExpandSize));
    }
  }

 private:
  void grow_chunk_table(std::size_t required_chunks) {
    if (required_chunks <= chunk_table_capacity_) {
      return;
    }

    std::size_t new_capacity = chunk_table_capacity_ == 0 ? 4 : chunk_table_capacity_;
    while (new_capacity < required_chunks) {
      if (new_capacity > std::numeric_limits<std::size_t>::max() / 2) {
        new_capacity = required_chunks;
        break;
      }
      new_capacity *= 2;
    }

    T **new_table = new T *[new_capacity];
    for (std::size_t i = 0; i < chunk_count_; ++i) {
      new_table[i] = chunks_[i];
    }
    delete[] chunks_;
    chunks_ = new_table;
    chunk_table_capacity_ = new_capacity;
  }

  void release_storage() noexcept {
    clear();
    for (std::size_t i = 0; i < chunk_count_; ++i) {
      allocator_traits::deallocate(allocator_, chunks_[i], ExpandSize);
    }
    delete[] chunks_;
    chunks_ = nullptr;
    chunk_count_ = 0;
    chunk_table_capacity_ = 0;
  }

  void take_storage(FixedVector &other) noexcept {
    chunks_ = std::exchange(other.chunks_, nullptr);
    chunk_count_ = std::exchange(other.chunk_count_, 0);
    chunk_table_capacity_ = std::exchange(other.chunk_table_capacity_, 0);
    size_ = std::exchange(other.size_, 0);
  }

  allocator_type allocator_;
  T **chunks_ = nullptr;
  std::size_t chunk_count_ = 0;
  std::size_t chunk_table_capacity_ = 0;
  std::size_t size_ = 0;
};

}  // namespace basic

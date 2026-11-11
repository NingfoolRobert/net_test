/**
 * @file fstring.h
 * @brief  string of fixed length
 * @author bfning
 * @version 0.1
 * @date 2025-01-01
 */
#pragma once

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace detail {

template <size_t N>
class fstring {
  static_assert(N > 0, "fstring capacity must be positive");
  public:
  fstring() {
    memset(data_, 0, N);
  }
  ~fstring() = default;
  explicit fstring(const char *other) {
    if (other != nullptr) {
      // memmove(data_, other, N - 1);
      strncpy(data_, other, N);
    }
    data_[N - 1] = 0;
  }

  explicit fstring(const std::string &other) {
    strncpy(data_, other.c_str(), N - 1);
    data_[N - 1] = 0;
  }

  explicit fstring(const std::string_view &other) {
    const auto count = std::min(other.size(), N - 1);
    if (count) memcpy(data_, other.data(), count);
    data_[count] = 0;
  }

  fstring(const char ch, size_t n) {
    auto num = std::min(n, N);
    memset(data_, ch, num);
    if (num < N) {
      data_[num] = 0;
    }
    if (n >= N) {
      data_[N - 1] = 0;
    }
  }

  fstring(const fstring &rhs) {
    memcpy(data_, rhs.data_, N);
  }
  //
  fstring(fstring &&rhs) noexcept {
    memmove(data_, rhs.data_, N);
    //clear the source to prevent 
    memset(rhs.data_, 0, N);
  }

  fstring& operator=(fstring &&rhs)  noexcept {
    if (this != &rhs) {
      memcpy(data_, rhs.data_, N);
      memset(rhs.data_, 0, N);
    }
    return *this;
  }
  fstring &operator=(const char *other) {
    if (other != nullptr) {
      const auto count = strnlen(other, N - 1);
      if (count != 0) memmove(data_, other, count);
      memset(data_ + count, 0, N - count);
    }
    data_[N - 1] = 0;
    return *this;
  }

  fstring &operator=(const std::string &other) {
    const auto count = std::min(other.size(), N - 1);
    if (count != 0) memmove(data_, other.data(), count);
    memset(data_ + count, 0, N - count);
    return *this;
  }

  fstring &operator=(const fstring &other) {
    if (this != &other) {
      memcpy(data_, other.data_, N);
    }
    return *this;
  }

  template <size_t N2>
  fstring &operator+(const fstring<N2> &rhs) {
    return append(rhs.c_str(), rhs.length());
  }

  bool operator==(const char *other) const {
    return other != nullptr && std::string_view(*this) == other;
  }

  bool operator<(const char *other) const {
    return other == nullptr || std::string_view(*this) < other;
  }

  bool operator>(const char *other) const {
    return other != nullptr && std::string_view(*this) > other;
  }

  bool operator==(const fstring &rhs) const {
    return std::string_view(*this) == std::string_view(rhs);
  }

  bool operator<(const fstring &rhs) const {
    return std::string_view(*this) < std::string_view(rhs);
  }

  bool operator>(const fstring &rhs) const {
    return std::string_view(*this) > std::string_view(rhs);
  }

  bool operator==(const std::string &rhs) const {
    return std::string_view(*this) == std::string_view(rhs);
  }

  bool operator<(const std::string &rhs) const {
    return std::string_view(*this) < std::string_view(rhs);
  }

  bool operator>(const std::string &rhs) const {
    return std::string_view(*this) > std::string_view(rhs);
  }

  fstring &operator+(const std::string &rhs) {
    return append(rhs.c_str(), rhs.length());
  }

  fstring &operator+(const char *rhs) {
    return append(rhs, strlen(rhs));
  }

  fstring &operator+(const char ch) {
    return append(&ch, 1);
  }

  fstring &operator+=(const fstring &rhs) {
    return append(rhs.c_str(), rhs.length());
  }

  char &operator[](size_t idx) {
    if (idx >= N) {
      throw std::runtime_error("fstring::operator[] idx invalid(exceed capibility)");
    }
    return data_[idx];
  }

  const char &operator[](size_t idx) const {
    if (idx >= N) {
      throw std::runtime_error("fstring::operator[] idx invalid(exceed capibility)");
    }
    return data_[idx];
  }

  std::string str() const {
    return to_string();
  }

  size_t capacity() const {
    return N;
  }
  //
  size_t length() const {
    return strnlen(data_, N);
  }

  size_t size() const {
    return length();
  }

  char *data() {
    return data_;
  }

  [[nodiscard]] const char *data() const {
    return data_;
  }

  [[nodiscard]] const char *c_str() const {
    return data_;
  }

  friend void swap(fstring &lft, fstring &rht) noexcept {
    lft.swap(rht);
  }

  fstring &append(const void *data, size_t size) {
    if (data == nullptr || size == 0) {
      return *this;
    }
    //
    auto len = length();
    if (size >= N - len) {
      return *this;
    }
    //
    memcpy(data_ + len, data, size);
    data_[size + len] = 0;
    return *this;
  }

  char &at(size_t idx) {
    if (idx >= N) {
      throw std::runtime_error("fstring::at, idx invalid: exceed  size");
    }
    return data_[idx];
  }

  const char &at(size_t idx) const {
    if (idx >= N) {
      throw std::runtime_error("fstring::at, idx invalid: exceed  size");
    }
    return data_[idx];
  }

  std::string substr(size_t idx, size_t n) const {
    return std::string(std::string_view(*this).substr(idx, n));
  }

  static constexpr auto npos{static_cast<size_t>(-1)};

  size_t find_first_of(char ch, size_t off = 0) const {
    return std::string_view(*this).find(ch, off);
  }

  size_t find_first_not_of(char ch, size_t off = 0) const {
    return std::string_view(*this).find_first_not_of(ch, off);
  }

  [[nodiscard]] bool empty() const {
    return data_[0] == 0;
  }

  void swap(fstring &rhs) noexcept {
    std::swap(data_, rhs.data_);
  }

  explicit operator bool() const {
    return !empty();
  }

  explicit operator std::string() const {
    return to_string();
  }

  explicit operator std::string_view() const {
    return std::string_view(data_, length());
  }

  [[nodiscard]] std::string to_string() const {
    return std::string(data_, length());
  }

  private:
  char data_[N]{};
};

//
template <size_t N>
std::ostream &operator<<(std::ostream &out, const fstring<N> &rhs) {
  out << rhs.str();
  return out;
}

template <size_t N1, size_t N2>
bool operator==(const fstring<N1> &lhs, const fstring<N2> &rhs) {
  return std::string_view(lhs) == std::string_view(rhs);
}

template <size_t N>
bool operator==(const std::string &lhs, const fstring<N> &rhs) {
  return std::string_view(lhs) == std::string_view(rhs);
}

template <size_t N>
bool operator==(const char *lhs, const fstring<N> &rhs) {
  return rhs == lhs;
}

}  // namespace detail

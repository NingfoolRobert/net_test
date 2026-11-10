/**
 * @file io.h
 * @brief Common CRTP I/O lifecycle, configuration and error handling.
 */
#pragma once

#include <cerrno>
#include <cstddef>
#include <limits>
#include <utility>

#include "error_state.h"

namespace detail::io {

template <typename Derived, typename Options>
class io : public error_state {
 public:
  bool open() {
    return call_open([&] { return impl()->open_impl(); });
  }

  void close() {
    call_close([&] { impl()->close_impl(); });
  }

  void set_opt(const Options& opt) { opt_ = opt; }

  Options& get_opt() { return opt_; }
  const Options& get_opt() const { return opt_; }

 protected:
  io() = default;
  io(const io&) = default;
  io& operator=(const io&) = default;
  // Resource owners close before their members are destroyed.
  ~io() = default;

  Derived* impl() { return static_cast<Derived*>(this); }

  template <typename Function>
  int call_transfer(const void* data, std::size_t len, Function&& function) {
    return call_io([&] {
      // Results use int, so reject requests that could overflow that result.
      if (len > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        errno = EOVERFLOW;
        return -1;
      }
      if (data == nullptr && len != 0) {
        errno = EINVAL;
        return -1;
      }
      return std::forward<Function>(function)();
    });
  }

  Options opt_{};
};
}  // namespace detail::io

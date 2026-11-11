/**
 * @file auto_ptr.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-06-28
 */
#pragma once

#include <type_traits>
#include <utility>

namespace detail {
template <typename T, typename Func>
class AutoPtr {
  public:
  AutoPtr(T *obj, Func &&fnc)
      : obj_(obj), fnc_(std::move(fnc)) {

        };

  ~AutoPtr() {
    if (obj_ != nullptr) {
      fnc_(obj_);
    }
  }

  AutoPtr(const AutoPtr &)            = delete;
  AutoPtr &operator=(const AutoPtr &) = delete;

  AutoPtr(AutoPtr &&other) noexcept(std::is_nothrow_move_constructible<Func>::value)
      : obj_(other.obj_), fnc_(std::move(other.fnc_)) {
    other.obj_ = nullptr;
  }

  AutoPtr &operator=(AutoPtr &&other) noexcept(std::is_nothrow_move_assignable<Func>::value) {
    if (this != &other) {
      if (obj_ != nullptr) {
        fnc_(obj_);
      }
      obj_       = nullptr;
      fnc_       = std::move(other.fnc_);
      obj_       = other.obj_;
      other.obj_ = nullptr;
    }
    return *this;
  }

  private:
  T   *obj_;
  Func fnc_;
};

template <typename Func>
class ScopeFunc {
  public:
  explicit ScopeFunc(Func &&fnc) : fnc_(std::move(fnc)), active_(true) {}

  ~ScopeFunc() {
    if (active_) {
      fnc_();
    }
  }

  ScopeFunc(const ScopeFunc &)            = delete;
  ScopeFunc &operator=(const ScopeFunc &) = delete;

  ScopeFunc(ScopeFunc &&other) noexcept(std::is_nothrow_move_constructible<Func>::value)
      : fnc_(std::move(other.fnc_)), active_(other.active_) {
    other.active_ = false;
  }

  ScopeFunc &operator=(ScopeFunc &&other) noexcept(std::is_nothrow_move_assignable<Func>::value) {
    if (this != &other) {
      if (active_) {
        fnc_();
      }
      fnc_          = std::move(other.fnc_);
      active_       = other.active_;
      other.active_ = false;
    }
    return *this;
  }

  private:
  Func fnc_;
  bool active_{};
};
}  // namespace detail

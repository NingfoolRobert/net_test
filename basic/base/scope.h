/**
 * @file scope.h
 * @author bfning
 * @version 0.1
 * @date 2025-08-09
 * @brief
 */
#pragma once

#include <type_traits>
#include <utility>

namespace detail {
template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F func) noexcept(std::is_nothrow_move_constructible_v<F>)
        : onExitScope_(std::move(func)), dismissed_(false) {
    }

    ~ScopeGuard() noexcept {
        if (!dismissed_) {
            onExitScope_();
        }
    }
    void dismiss() noexcept {
        dismissed_ = true;
    }

    ScopeGuard(const ScopeGuard &) = delete;

    ScopeGuard &operator=(const ScopeGuard &) = delete;

    ScopeGuard(ScopeGuard &&other) noexcept(std::is_nothrow_move_constructible_v<F>)
        : onExitScope_(std::move(other.onExitScope_)), dismissed_(other.dismissed_) {
        other.dismiss();
    }

    ScopeGuard &operator=(ScopeGuard &&) = delete;

private:
    F onExitScope_;
    bool dismissed_;
};

template <typename F>
ScopeGuard<std::decay_t<F>> make_scope(F &&func) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(func));
}
}  // namespace detail

/**
 * @file scope.h
 * @author bfning
 * @version 0.1
 * @date 2025-08-09
 * @brief
 */
#pragma once

namespace detail {
template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F &&func) noexcept : onExitScope_(std::move(func)), dismissed_(false) {
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

    ScopeGuard(ScopeGuard &&) {
        dismissed_ = other.dismissed_;
        other.dismissed_ = true;
        onExitScope_ = std::move(other.onExitScope_);
    }

    ScopeGuard &operator=(ScopeGuard &&) {
        if (this != &other) {
            dismissed_ = other.dismissed_;
            other.dismissed_ = true;
            onExitScope_ = std::move(other.onExitScope_);
        }
        return *this;
    }

private:
    F onExitScope_;
    bool dismissed_;
};

template <typename F>
ScopeGuard<F> make_scope(F &&func) {
    return ScopeGuard<F>(std::forward<F>(func));
}
}  // namespace detail

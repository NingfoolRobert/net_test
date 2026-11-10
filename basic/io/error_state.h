#pragma once

#include "error_code.h"
#include <atomic>
#include <cerrno>
#include <utility>

namespace detail::io {

// Per-instance snapshot of the most recently completed operation. errno is
// thread-local and may be overwritten by unrelated calls; snapshot it before
// returning to the caller. Concurrent operations share the latest snapshot.
class error_state {
public:
    int last_error() const noexcept { return last_error_.load(std::memory_order_relaxed); }
    ErrorCode error_code() const noexcept { return ErrorCode(last_error()); }

protected:
    error_state() = default;
    error_state(const error_state &other) noexcept : last_error_(other.last_error()) {}
    error_state &operator=(const error_state &other) noexcept {
        last_error_.store(other.last_error(), std::memory_order_relaxed);
        return *this;
    }
    template <typename Function>
    bool call_open(Function &&function) {
        errno = 0;
        const bool success = std::forward<Function>(function)();
        const int code = errno;
        last_error_.store(success ? 0 : (code ? code : EIO), std::memory_order_relaxed);
        return success;
    }
    template <typename Function>
    int call_io(Function &&function) {
        errno = 0;
        const int result = std::forward<Function>(function)();
        const int code = errno;
        last_error_.store(result < 0 ? (code ? code : EIO) : 0, std::memory_order_relaxed);
        return result;
    }
    template <typename Function>
    void call_close(Function &&function) {
        errno = 0;
        std::forward<Function>(function)();
        last_error_.store(errno, std::memory_order_relaxed);
    }
private:
    std::atomic<int> last_error_{0};
};
}  // namespace detail::io

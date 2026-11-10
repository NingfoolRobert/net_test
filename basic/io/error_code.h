#pragma once

#include <cerrno>
#include <cstring>
#include <string>

namespace detail::io {

// I/O error value, independent of the originating object and its later calls.
// Native errno values are retained; zero represents success.
class ErrorCode {
public:
    constexpr ErrorCode() noexcept = default;
    explicit constexpr ErrorCode(int value) noexcept : value_(value) {}

    constexpr int value() const noexcept { return value_; }
    constexpr bool has_error() const noexcept { return value_ != 0; }
    explicit constexpr operator bool() const noexcept { return has_error(); }

    // Return an owned description, preserving this thread's errno even if
    // formatting or allocation throws. Handle both POSIX and GNU strerror_r.
    std::string message() const {
        struct RestoreErrno {
            int saved = errno;
            ~RestoreErrno() { errno = saved; }
        } restore;
        char buffer[256]{};
#ifdef _WIN32
        return description(::strerror_s(buffer, sizeof(buffer), value_), buffer);
#else
        return description(::strerror_r(value_, buffer, sizeof(buffer)), buffer);
#endif
    }

    friend constexpr bool operator==(ErrorCode lhs, ErrorCode rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }
    friend constexpr bool operator!=(ErrorCode lhs, ErrorCode rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    std::string description(int result, const char *buffer) const {
        return result == 0 ? std::string(buffer) : unknown_message();
    }
    std::string description(const char *result, const char *) const {
        return result ? std::string(result) : unknown_message();
    }
    std::string unknown_message() const {
        return "Unknown I/O error (" + std::to_string(value_) + ")";
    }
    int value_ = 0;
};
}  // namespace detail::io

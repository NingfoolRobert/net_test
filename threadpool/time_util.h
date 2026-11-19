/**
 * @file time_util.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-05
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <time.h>

namespace detail::time_util {

constexpr int64_t PER_MICROSECONDS = 1000LL;
constexpr int64_t PER_MILLISECONDS = 1000LL * PER_MICROSECONDS;
constexpr int64_t PER_SECONDS = 1000LL * PER_MILLISECONDS;
constexpr int64_t PER_MINUTE = 60LL * PER_SECONDS;
constexpr int64_t PER_HOUR = 60LL * PER_MINUTE;
constexpr int64_t PER_DAY = 24LL * PER_HOUR;

int64_t now() {
    struct timespec now;
    if (clock_gettime(CLOKC_MONOTONIC, &now) == -1) {
        return 0;
    }
    return now.tv_sec * PER_SECONDS + now.tv_nsec;
}

}  // namespace detail::util

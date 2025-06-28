/**
 * @file date_time.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-28
 */

#ifndef _DATE_TIME_H_
#define _DATE_TIME_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "optimization.h"

namespace detail {

using tick_t = uint64_t;
static constexpr tick_t TICKS_PER_US = 1000ULL;
static constexpr tick_t TICKS_PER_MS = 1000ULL * TICKS_PER_US;
static constexpr tick_t TICKS_PER_SECOND = 1000ULL * TICKS_PER_MS;
static constexpr tick_t TICKS_PER_MINUTE = 60ULL * TICKS_PER_SECOND;
static constexpr tick_t TICKS_PER_HOUR = 60ULL * TICKS_PER_MINUTE;
static constexpr tick_t TICKS_PER_DAY = 24ULL * TICKS_PER_HOUR;

static inline tick_t now() {
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    return tp.tv_sec * TICKS_PER_SECOND + tp.tv_nsec;
}

static inline tick_t utc_to_tick(time_t ts) {
    return TICKS_PER_SECOND * ts;
}

static inline std::string tick_to_timestamp(tick_t tick, const char *strformat) {
    struct tm tp;
    time_t t = tick / TICKS_PER_SECOND;
    localtime_r(&t, &tp);
    //
    char out[64] = {0};
    strftime(out, 63, strformat, &tp);
    return out;
}

static inline tick_t timestamp_to_tick(int year, int month, int mday, int hour, int minute, int sec, int milli) {
    struct tm tp{
        .tm_sec = sec, .tm_min = minute, .tm_hour = hour, .tm_mday = mday, .tm_mon = month - 1, .tm_year = year - 1900};
    //
    tick_t time_now = mktime(&tp) * TICKS_PER_SECOND;
    return time_now + TICKS_PER_MS * milli;
}

}  // namespace detail

#endif

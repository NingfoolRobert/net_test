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
#include <sys/time.h>
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


static FORCE_INLINE tick_t now() {
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    return tp.tv_sec * TICKS_PER_SECOND + tp.tv_nsec ;
}

static inline tick_t utc_to_tick(time_t ts) {
    return ts * TICKS_PER_SECOND;
}

static inline tick_t timestamp_to_tick(int year, int month, int mday, int hour, int minute, int sec, int milli) {
    struct tm tp;  // = {.year = year, .month = mon - 1, .day=mday, }

    return 0;
}

}  // namespace detail

#endif

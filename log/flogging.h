/**
 * @file flogging.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-04-12
 */
#ifndef _FLOGGING_H_
#define _FLOGGING_H_
#pragma once

#include "fmt/core.h"
#include "fmt/format.h"

static const char *log_type_flag[] = {"", "TRACE", "DEBUG", "NOTICE", "INFO", "WARN", "ERROR", "FATAL"};
#ifndef _WIN32
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
static inline const char *base_file_name(const char *file_full_name) {
    auto ptr = strrchr(file_full_name, '/');
    return nullptr == ptr ? ptr : ptr + 1;
}

pid_t gettid() {
    return syscall(SYS_gettid);
}

#define SYS_FLOG_PRINT(log_level, format, ...)                                                                         \
    do {                                                                                                               \
        struct timeval tv;                                                                                             \
        gettimeofday(&tv, nullptr);                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_r(&tv.tv_sec, &tmNow);                                                                               \
        fmt::println("{}-{:02}-{:02} {:02}:{:02}:{:02}.{:06} {} {} " format,                                           \
                     tmNow.tm_year + 1900,                                                                             \
                     tmNow.tm_mon + 1,                                                                                 \
                     tmNow.tm_mday,                                                                                    \
                     tmNow.tm_hour,                                                                                    \
                     tmNow.tm_min,                                                                                     \
                     tmNow.tm_sec,                                                                                     \
                     tv.tv_usec,                                                                                       \
                     gettid(),                                                                                         \
                     log_type_flag[log_level],                                                                         \
                     ##__VA_ARGS__);                                                                                   \
    } while (false)

#define SYS_FLOG_PRINTEX(log_level, format, ...)                                                                       \
    do {                                                                                                               \
        struct timeval tv;                                                                                             \
        gettimeofday(&tv, nullptr);                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_r(&tv.tv_sec, &tmNow);                                                                               \
        fmt::println("{}-{:02}-{:02} {:02}:{:02}:{:02}.{:06} {} {} [{}:{}] " format,                                   \
                     tmNow.tm_year + 1900,                                                                             \
                     tmNow.tm_mon + 1,                                                                                 \
                     tmNow.tm_mday,                                                                                    \
                     tmNow.tm_hour,                                                                                    \
                     tmNow.tm_min,                                                                                     \
                     tmNow.tm_sec,                                                                                     \
                     tv.tv_usec,                                                                                       \
                     gettid(),                                                                                         \
                     log_type_flag[log_level],                                                                         \
                     base_file_name(__FILE__),                                                                         \
                     __LINE__,                                                                                         \
                     ##__VA_ARGS__);                                                                                   \
    } while (false)
#else
#include <stdio.h>
#include <time.h>
#include <timeb.h>
#include <varargs.h>
#define SYS_FLOG_PRINT(log_level, format, ...)                                                                         \
    do {                                                                                                               \
        timeb tv;                                                                                                      \
        ftime(&tv);                                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_s(&tmNow, &tv.time);                                                                                 \
        fmt::println("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06} {} " format,                                           \
                     tmNow.tm_year + 1900,                                                                             \
                     tmNow.tm_mon + 1,                                                                                 \
                     tmNow.tm_mday,                                                                                    \
                     tmNow.tm_hour,                                                                                    \
                     tmNow.tm_min,                                                                                     \
                     tmNow.tm_sec,                                                                                     \
                     tv.millitm,                                                                                       \
                     log_type_flag[log_level],                                                                         \
                     ##__VA_ARGS__);                                                                                   \
        printf("\n");                                                                                                  \
    } while (false)

#define SYS_FLOG_PRINTEX(log_level, format, ...)                                                                       \
    do {                                                                                                               \
        timeb tv;                                                                                                      \
        ftime(&tv);                                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_s(&tmNow, &tv.time);                                                                                 \
        fmt::println("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06} {} [{}:{}] " format,                                   \
                     tmNow.tm_year + 1900,                                                                             \
                     tmNow.tm_mon + 1,                                                                                 \
                     tmNow.tm_mday,                                                                                    \
                     tmNow.tm_hour,                                                                                    \
                     tmNow.tm_min,                                                                                     \
                     tmNow.tm_sec,                                                                                     \
                     tv.millitm,                                                                                       \
                     log_type_flag[log_level],                                                                         \
                     __FILE__,                                                                                         \
                     __LINE__,                                                                                         \
                     ##__VA_ARGS__);                                                                                   \
        printf("\n");                                                                                                  \
    } while (false)
#endif

#ifdef SYS_FLOG
//////////
#define INIT_FLOG(log_level, log_dir)
//////////
#define FLOG_TRACE(...) SYS_FLOG_PRINT(1, __VA_ARGS__)
#define FLOG_DEBUG(...) SYS_FLOG_PRINT(2, __VA_ARGS__)
#define FLOG_NOTICE(...) SYS_FLOG_PRINT(3, __VA_ARGS__)
#define FLOG_INFO(...) SYS_FLOG_PRINT(4, __VA_ARGS__)
#define FLOG_WARN(...) SYS_FLOG_PRINTEX(5, __VA_ARGS__)
#define FLOG_ERROR(...) SYS_FLOG_PRINTEX(6, __VA_ARGS__)
#define FLOG_FATAL(...) SYS_FLOG_PRINTEX(7, __VA_ARGS__)
#elif defined ENABLE_QUILL_LOG
#define INIT_FLOG(log_level, log_dir)                                                                                  \
    do {                                                                                                               \
    } while (false)

#else
#define FLOG_DEBUG(...)
#define FLOG_TRACE(...)
#define FLOG_NOTICE(...)
#define FLOG_INFO(...)
#define FLOG_WARN(...)
#define FLOG_ERROR(...)
#define FLOG_FATAL(...)
#endif

#endif

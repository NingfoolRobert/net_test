/**
 * @file logging.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-10
 */
#ifndef _LOGGING_H_
#define _LOGGING_H_

static const char *log_type_str[] = {"", "TRACE", "DEBUG", "NOTICE", "INFO", "WARN", "ERROR", "FATAL"};
#ifndef _WIN32
#include <stdarg.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

pid_t gettid() {
    return syscall(SYS_gettid);
}

#define SYS_LOG_PRINT(log_level, fmt, ...)                                                                             \
    do {                                                                                                               \
        struct timeval tv;                                                                                             \
        gettimeofday(&tv, nullptr);                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_r(&tv.tv_sec, &tmNow);                                                                               \
        printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld %u [%s] " fmt,                                                     \
               tmNow.tm_year + 1900,                                                                                   \
               tmNow.tm_mon + 1,                                                                                       \
               tmNow.tm_mday,                                                                                          \
               tmNow.tm_hour,                                                                                          \
               tmNow.tm_min,                                                                                           \
               tmNow.tm_sec,                                                                                           \
               tv.tv_usec,                                                                                             \
               gettid(),                                                                                               \
               log_type_str[log_level],                                                                                \
               ##__VA_ARGS__);                                                                                         \
        printf("\n");                                                                                                  \
    } while (false)

#define SYS_LOG_PRINTEX(log_level, fmt, ...)                                                                           \
    do {                                                                                                               \
        struct timeval tv;                                                                                             \
        gettimeofday(&tv, nullptr);                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_r(&tv.tv_sec, &tmNow);                                                                               \
        printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld %u [%s] [%s:%d] " fmt,                                             \
               tmNow.tm_year + 1900,                                                                                   \
               tmNow.tm_mon + 1,                                                                                       \
               tmNow.tm_mday,                                                                                          \
               tmNow.tm_hour,                                                                                          \
               tmNow.tm_min,                                                                                           \
               tmNow.tm_sec,                                                                                           \
               tv.tv_usec,                                                                                             \
               gettid(),                                                                                               \
               log_type_str[log_level],                                                                                \
               __FILE__,                                                                                               \
               __LINE__,                                                                                               \
               ##__VA_ARGS__);                                                                                         \
        printf("\n");                                                                                                  \
    } while (false)
#else
#include <stdio.h>
#include <time.h>
#include <timeb.h>
#include <varargs.h>
#define SYS_LOG_PRINT(log_level, fmt, ...)                                                                             \
    do {                                                                                                               \
        timeb tv;                                                                                                      \
        ftime(&tv);                                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_s(&tmNow, &tv.time);                                                                                 \
        printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld [%s] " fmt,                                                        \
               tmNow.tm_year + 1900,                                                                                   \
               tmNow.tm_mon + 1,                                                                                       \
               tmNow.tm_mday,                                                                                          \
               tmNow.tm_hour,                                                                                          \
               tmNow.tm_min,                                                                                           \
               tmNow.tm_sec,                                                                                           \
               tv.millitm,                                                                                             \
               log_type_str[log_level],                                                                                \
               ##__VA_ARGS__);                                                                                         \
        printf("\n");                                                                                                  \
    } while (false)

#define SYS_LOG_PRINTEX(log_level, fmt, ...)                                                                           \
    do {                                                                                                               \
        timeb tv;                                                                                                      \
        ftime(&tv);                                                                                                    \
        struct tm tmNow;                                                                                               \
        localtime_s(&tmNow, &tv.time);                                                                                 \
        printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld [%s] [%s:%d]" fmt,                                                 \
               tmNow.tm_year + 1900,                                                                                   \
               tmNow.tm_mon + 1,                                                                                       \
               tmNow.tm_mday,                                                                                          \
               tmNow.tm_hour,                                                                                          \
               tmNow.tm_min,                                                                                           \
               tmNow.tm_sec,                                                                                           \
               tv.millitm,                                                                                             \
               log_type_str[log_level],                                                                                \
               __FILE__,                                                                                               \
               __LINE__,                                                                                               \
               ##__VA_ARGS__);                                                                                         \
        printf("\n");                                                                                                  \
    } while (false)
#endif

#ifdef SYS_LOG
//////////
#define INIT_LOG(log_level, log_dir)
//////////
#define NLOG_TRACE(...) SYS_LOG_PRINT(1, __VA_ARGS__)
#define NLOG_DEBUG(...) SYS_LOG_PRINT(2, __VA_ARGS__)
#define NLOG_NOTICE(...) SYS_LOG_PRINT(3, __VA_ARGS__)
#define NLOG_INFO(...) SYS_LOG_PRINT(4, __VA_ARGS__)
#define NLOG_WARN(...) SYS_LOG_PRINTEX(5, __VA_ARGS__)
#define NLOG_ERROR(...) SYS_LOG_PRINTEX(6, __VA_ARGS__)
#define NLOG_FATAL(...) SYS_LOG_PRINTEX(7, __VA_ARGS__)
#else
#define NLOG_DEBUG(...)
#define NLOG_TRACE(...)
#define NLOG_NOTICE(...)
#define NLOG_INFO(...)
#define NLOG_WARN(...)
#define NLOG_ERROR(...)
#define NLOG_FATAL(...)
#endif
#endif

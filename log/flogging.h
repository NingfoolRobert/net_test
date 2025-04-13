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

#include "nlog.h"

#ifndef _WIN32
#include "file_util.h"
#include "fmt/core.h"
#include "fmt/format.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

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
                     log_level_flag[log_level],                                                                        \
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
                     log_level_flag[log_level],                                                                        \
                     detail::path_base_name(__FILE__),                                                                 \
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
                     log_level_flag[log_level],                                                                        \
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
                     log_level_flag[log_level],                                                                        \
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
#define FLOG_TRACE(...) SYS_FLOG_PRINT(LogLevelEnum::LOG_EVEL_TRACE, __VA_ARGS__)
#define FLOG_DEBUG(...) SYS_FLOG_PRINT(LogLevelEnum::LOG_LEVEL_DEBUG, __VA_ARGS__)
#define FLOG_NOTICE(...) SYS_FLOG_PRINT(LogLevelEnum::LOG_LEVEL_NOTICE, __VA_ARGS__)
#define FLOG_INFO(...) SYS_FLOG_PRINT(LogLevelEnum::LOG_LEVEL_INFO, __VA_ARGS__)
#define FLOG_WARN(...) SYS_FLOG_PRINTEX(LogLevelEnum::LOG_LEVEL_WARN, __VA_ARGS__)
#define FLOG_ERROR(...) SYS_FLOG_PRINTEX(LogLevelEnum::LOG_LEVEL_ERROR, __VA_ARGS__)
#define FLOG_FATAL(...) SYS_FLOG_PRINTEX(LogLevelEnum::LOG_LEVEL_CRITICAL, __VA_ARGS__)
#elif defined ENABLE_QUILL_LOG
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/FileSink.h"

class QuillWrapper {
public:
    static QuillWrapper &get_instance() {
        static QuillWrapper wrapper;
        return wrapper;
    }

    ~QuillWrapper() = default;

    void set_logger(quill::Logger *logger) {
        logger_ = logger;
    }
    //
    quill::Logger *logger() {
        return logger_;
    }

    void set_log_level(LogLevelEnum log_level) {
        static std::map<LogLevelEnum, quill::LogLevel> log_level_mapping = {
            {LogLevelEnum::LOG_LEVEL_DEBUG, quill::LogLevel::Debug},
            {LogLevelEnum::LOG_LEVEL_INFO, quill::LogLevel::Info},
            {LogLevelEnum::LOG_LEVEL_WARN, quill::LogLevel::Warning},
            {LogLevelEnum::LOG_LEVEL_ERROR, quill::LogLevel::Error},
            {LogLevelEnum::LOG_LEVEL_CRITICAL, quill::LogLevel::Critical},
        };
        logger_->set_log_level(log_level_mapping[log_level]);
    }

private:
    QuillWrapper() = default;
    quill::Logger *logger_{nullptr};
};

#define INIT_FLOG(log_level, log_dir, app_name)                                                                        \
    do {                                                                                                               \
        quill::BackendOptions backend_options;                                                                         \
        quill::Backend::start(backend_options);                                                                        \
        auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(                                         \
            app_name,                                                                                                  \
            []() {                                                                                                     \
                quill::FileSinkConfig cfg;                                                                             \
                cfg.set_open_mode('w');                                                                                \
                cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);                            \
                return cfg;                                                                                            \
            }(),                                                                                                       \
            quill::FileEventNotifier{});                                                                               \
        quill::Logger *logger = quill::Frontend::create_or_get_logger(                                                 \
            "root",                                                                                                    \
            std::move(file_sink),                                                                                      \
            quill::PatternFormatterOptions{                                                                            \
                "%(time) [%(thread_id)] %(short_source_location:<28) %(log_level:<9) %(message)",                      \
                "%Y%m%d %H:%M:%S.%Qns",                                                                                \
                quill::Timezone::GmtTime});                                                                            \
        QuillWrapper::get_instance().set_logger(logger);                                                               \
        QuillWrapper::get_instance().set_log_level(log_level);                                                         \
    } while (false)

#define FLOG_DEBUG(...) LOG_DEBUG(QuillWrapper::get_instance().logger(), ##__VA_ARGS__)
#define FLOG_TRACE(...) LOG_TRACE(QuillWrapper::get_instance().logger(), ##__VA_ARGS__)
#define FLOG_NOTICE(...) LOG_NOTIC(QuillWrapper::get_instance().logger(), ##__VA_ARGS__)
#define FLOG_INFO(...) LOG_INFO(QuillWrapper::get_instance().logger(), ##__VA_ARGS__)
#define FLOG_WARN(...) LOG_WARN(QuillWrapper::get_instance().logger(), ##__VA_ARGS__)
#define FLOG_ERROR(...) LOG_ERROR(QuillWrapper::get_instance().logger(), ##__VA_ARGS__)
#define FLOG_FATAL(...) LOG_CRITICAL(QuillWrapper::get_instance().logger(), ##__VA_ARGS__)
#else
#define INIT_FLOG(log_level, log_dir, app_name)

#define FLOG_DEBUG(...)
#define FLOG_TRACE(...)
#define FLOG_NOTICE(...)
#define FLOG_INFO(...)
#define FLOG_WARN(...)
#define FLOG_ERROR(...)
#define FLOG_FATAL(...)
#endif

#endif

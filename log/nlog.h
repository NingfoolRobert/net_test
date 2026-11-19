/**
 * @file nlog.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-04-12
 */
#ifndef _NLOG_H_
#define _NLOG_H_
#pragma once

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

enum LogLevelEnum : uint8_t
{
    LOG_LEVEL_TRACE = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_INFO = 5,
    LOG_LEVEL_NOTICE = 6,
    LOG_LEVEL_WARN = 7,
    LOG_LEVEL_ERROR = 8,
    LOG_LEVEL_CRITICAL = 9,
    LOG_LEVEL_MAX
};

static const char *log_level_flag[] = {"ALL", "", "", "TRACE", "DEBUG", "INFO", "NOTICE", "WARN", "ERROR", "CRITICAL"};

class NSink {
public:
    virtual void init() = 0;
    virtual void uninit() = 0;
    virtual void write_log(LogLevelEnum log_level, const char *fmt, ...) = 0;
};

class NLog {
public:
    static NLog &GetInstance() {
        static NLog nlog;
        return nlog;
    }
    //
    ~NLog() = default;
    //
    void start() {
    }

    void stop() {
    }
    //
    void set_log_level(const LogLevelEnum level) {
        log_level_ = level;
    }
    //
    void WriteLog(const LogLevelEnum log_level, const char *pszFmt, ...) {
        if (log_level < log_level_) {
            return;
        }
        // TODO
    }

private:
    NLog() = default;

private:
    LogLevelEnum log_level_{LOG_LEVEL_INFO};
    std::string log_dir_;

    std::vector<NSink *> sinks_;
};

#endif

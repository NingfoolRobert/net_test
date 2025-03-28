/**
 * @file assert_marco.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-09
 */
#pragma once
#ifndef _ASSERT_MACRO_H_
#define _ASSERT_MACRO_H_
#include "logging.h"
#include "optimization.h"

#define assert_nullptr(cond, ret)                                                                                      \
    do {                                                                                                               \
        if (UNLIKELY(cond == nullptr)) {                                                                               \
            NLOG_WARN("CHECK FAIL: nullptr == "#cond);                                                                   \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (false)

#define assert_nullptr2(cond)                                                                                          \
    do {                                                                                                               \
        if (UNLIKELY(cond == nullptr)) {                                                                               \
            NLOG_WARN("CHECK FAIL: nullptr == "#cond);                                                                   \
            return;                                                                                                    \
        }                                                                                                              \
    } while (false)

#define assert_true(cond, ret)                                                                                         \
    do {                                                                                                               \
        if (UNLIKELY(!cond)) {                                                                                         \
            NLOG_WARN("CHECK FAIL: false == "#cond);                                                                   \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (false)

#define assert_true2(cond)                                                                                             \
    do {                                                                                                               \
        if (UNLIKELY(!(cond))) {                                                                                       \
            NLOG_WARN("CHECK FAIL: false == "#cond);                                                                   \
            return;                                                                                                    \
        }                                                                                                              \
    } while (false)

#endif

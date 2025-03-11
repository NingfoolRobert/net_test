/**
 * @file assert_marco.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-09
 */
#pragma once

#include "optimization.h"

#define assert_nullptr(cond, ret)                                                                                      \
    do {                                                                                                               \
        if (UNLIKELY(cond == nullptr)) {                                                                             \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (false)

#define assert_ture(cond, ret)                                                                                         \
    do {                                                                                                               \
        if (UNLIKELY(!(cond))) {                                                                                       \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (false)



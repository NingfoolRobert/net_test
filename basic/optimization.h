/**
 * @file optimization.h
 * @brief
 * @author
 * @version
 * @date 2025-03-05
 */
#ifndef _OPTIMIZATION_H_
#define _OPTIMIZATION_H_

#if __has_builtin(__builtin_expect)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#ifndef FORCE_INLINE
#define FORCE_INLINE __attribute__((always_inline))
#endif

#endif

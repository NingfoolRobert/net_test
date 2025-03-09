/**
 * @file optimization.h
 * @brief
 * @author
 * @version
 * @date 2025-03-05
 */
#pragma once

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#if __GNUC__ > 7
#define LIKELY(x) [[likely]] (x)
#define UNLIKELY(x) [[unlikely]] (x)
#else
#define LIKELY(x) __builtin_expect((x), true)
#define UNLIKELY(x) __builtin_expect(!(x), true)
#endif
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

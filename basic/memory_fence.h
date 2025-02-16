/**
 * @file memory_fence.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-12
 */
#pragma once
#include <cstdio>

namespace detail {
class MemoryFence {
public:
    MemoryFence() = default;
    ~MemoryFence() = default;

public:
    static void inline lfence() {
        __asm__ __volatile__("lfence" ::: "memory");
    }

    static void inline sfence() {
        __asm__ __volatile__("sfence" ::: "memory");
    }

    static void inline enter() {
        lfence();
    }

    static void inline leave() {
        sfence();
        lfence();
    }
    
    static void inline fence() {
      __asm__ __volatile__("" ::: "memory");
    }
};
}  // namespace detail

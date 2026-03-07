/**
 * @file memory_fence.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-12
 */
#pragma once

namespace detail {
class MemoryFence {
public:
    MemoryFence() = default;
    ~MemoryFence() = default;

    MemoryFence(const MemoryFence&) = delete;
    MemoryFence& operator=(const MemoryFence&) = delete;
    MemoryFence(MemoryFence&&) = delete;
    MemoryFence& operator=(MemoryFence&&) = delete;

public:
    static inline void  lfence() {
        __asm__ __volatile__("lfence" ::: "memory");
    }

    static void  sfence() {
        __asm__ __volatile__("sfence" ::: "memory");
    }

    static void  enter() {
        lfence();
    }

    static void  leave() {
        sfence();
        lfence();
    }
    
    static void  fence() {
      __asm__ __volatile__("" ::: "memory");
    }
};
}  // namespace detail

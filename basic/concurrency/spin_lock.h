/**
 * @file	spin_lock.h
 * @brief
 * @author	ningbf
 * @version 1.0.0
 * @date	2023-03-08
 */
#ifndef _SPIN_LOCK_HPP_
#define _SPIN_LOCK_HPP_

#include <atomic>
#include <exception>
#include <immintrin.h>
#include <sched.h>

class NullLock {
    NullLock() = default;
    ~NullLock() = default;
    NullLock(const NullLock &) = delete;
    NullLock &operator=(const NullLock &) = delete;

    void lock() {
    }
    bool try_lock() {
        return true;
    }
    void unlock() {
    }
};

class SpinLock {
public:
    SpinLock() {
    }
    ~SpinLock() {
    }
    //
    SpinLock(const SpinLock &) = delete;
    SpinLock &operator=(const SpinLock &) = delete;
    //
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            sched_yield();
        }
    }
    //
    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }
    //
    void unlock() {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

#endif

/**
 * @file tsc.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-07-12
 */
#pragma once
#include <cstdint>
#include <ctime>
#include <sys/time.h>
#include <unistd.h>

namespace detail {

class RDTSC {
public:
    static RDTSC &instance() {
        static RDTSC tsc;
        return tsc;
    }
    //
    static uint64_t now_cycle() {
        unsigned int lo = 0;
        unsigned int hi = 0;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }

    [[nodiscard]] uint64_t now() const {
        auto cycle_span = now_cycle() - cycle_base_;
        auto ns_delta = static_cast<uint64_t>(static_cast<long double>(cycle_span) / frequence_);
        return timepoint_base_ + ns_delta;
    }
    //  one cycle duration : ns
    double cycle_freq() const {
        return frequence_;
    }

private:
    RDTSC() {
        init();
    }

    ~RDTSC() = default;
    //
    void init() {
        cycle_base_ = now_cycle();
        frequence_ = calibrate();
        struct timespec tp{};
        clock_gettime(CLOCK_REALTIME, &tp);
        timepoint_base_ = tp.tv_sec * 1000000000ULL + tp.tv_nsec;
    }
    //
    static double calibrate() {
        struct timespec start{};
        struct timespec end{};
        clock_gettime(CLOCK_MONOTONIC, &start);
        uint64_t tsc_start = now_cycle();
        usleep(1 * 1000 * 1000);  // 延时1秒
        clock_gettime(CLOCK_MONOTONIC, &end);
        uint64_t tsc_end = now_cycle();
        auto time_span = ((end.tv_sec - start.tv_sec) * 1000000000ULL) + end.tv_nsec - start.tv_nsec;
        return static_cast<double>(tsc_end - tsc_start) / time_span;
    }

private:
    double frequence_{0};
    uint64_t cycle_base_{0};
    uint64_t timepoint_base_{0};
};

#define rdtsc_dtl RDTSC::GetInstance()
}  // namespace detail

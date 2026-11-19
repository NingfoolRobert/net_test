/**
 * @file thread.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-11
 */
#pragma once

#include "executor.h"
#include <thread>

namespace detail {

class thread_t {
public:
    struct thread_config {
        int8_t affinity{-1};
        uint8_t priority{50};
        int16_t timeout{10000};
        int32_t realtime_type;
    };
    //
    thread_t() {
    }
    //
    ~thread_t() {
    }
    //
    void run() {
        thr_.reset(new std::thread([&]() { exe_.loop(cfg_.timeout); }));
    }

private:
    thread_config cfg_;
    executor_t exec_;
    std::unique_ptr<std::thread> thr_;
};
}  // namespace detail

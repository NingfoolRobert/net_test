/**
 * @file sync_waiter.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2024-12-29
 */
#pragma once
#include <future>
#include <stdint.h>
#include <stdio.h>

template <typename T>
class Synchronizer {

public:
    Synchronizer() {
        future_ = promise_.get_future();
    }
    ~Synchronizer() = default;

public:
    void wait() {
        future_.wait();
    }

    T wait(size_t timeout) {
        auto ret = future_.wait_for(std::chrono::microseconds(timeout));
        if (ret == std::future_status::timeout) {
            return {};
        }
        return future_.get();
    }

    void notify(T v) {
        promise_.set_value(v);
    }
    
    T get() {
        return future_.get();
    }
private:
    std::promise<T> promise_;
    std::future<T> future_;
};

    

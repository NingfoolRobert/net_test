/**
 * @file timer_queue.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-01-05
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <list>
#include <atomic>

namespace detail {
//
using timer_id = int64_t;
using timer_callback = std::function<void(timer_id, int64_t)>;
//
class timer_queue {
public:
    struct event_t {
        timer_id id;
        int32_t count;
        int32_t interval;
        int64_t expired;
        timer_callback cb;
        event_t(int64_t expired, timer_callback&& cb):interval(0), expired(expired), cb(cb) {
        }
    };
public:
    timer_id add(int64_t expired, timer_callback&& cb);
    
    timer_id add(int32_t count, int32_t  interval, timer_callback&& cb);

    void remove(timer_id id);

    void loop();
    
    static timer_id   fetch_id() {
      return  ++timer_id_; 
    }
private:
    void process_timer();

public:
    static std::atomic_int64_t timer_id_;
private:
    int64_t               last_;
    std::list<event_t>    timer_slot_[5];
};
//
using timer_event = timer_queue::event_t;
}  // namespace detail

#include "timer_queue.h"
#include "time_util.h"

namespace detail {

static std::atomic_int64_t timer_id_ = 0;
timer_id timer_queue::add(int64_t expired, timer_callback &&cb) {
    timer_event evt(expired, std::move(cb));
    evt.id = fetch_id();
    
    auto now = time_util::now();
    auto diff = now - expired;
    if(diff <= 0){
      cb();
    }
    else if(diff <= time_util::PER_SECONDS) {
      
    }
    else if(diff <= time_util::PER_MINUTE) {

    }else if(diff <= time_util::PER_HOUR){
      
    }else if(diff <= time_util::PER_DAY) {
      
    }else {
      
    }
    
    
}

timer_id timer_queue::add(int32_t count, int32_t interval, timer_callback &&cb) {
}

void timer_queue::loop() {

}
    
void timer_queue::process_timer(){
  auto now = time_util::now();
}
}  // namespace detail

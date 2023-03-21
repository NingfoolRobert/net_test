#include "time.hpp"
#include <chrono>
		
namespace detail{
time::time(){
	auto now = std::chrono::system_clock::now();
	start_time_steady_ = std::chrono::steady_clock::now().time_since_epoch().count();
	start_time_since_epoch_ = std::chrono::duration<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

time& time:: get_instance(){
	static time t;
	return t;
}
		
int64_t time::now(){
	auto d = std::chrono::steady_clock::now().time_since_epoch().count() - get_instance().start_time_steady_;
	return get_instance().start_time_since_epoch_ + d;	
}
}

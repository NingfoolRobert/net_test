#include "simple_token.h"
#include "time.hpp"
#include <chrono>


simple_token::simple_token(uint32_t rate)
{
	_rate = detail::time_unit::NANOSECONDS_PER_SECOND / rate;
}

simple_token::~simple_token()
{

}

void simple_token::wait()
{
	while (true) {
		auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		if (_time_point + _rate <= now) {
			_time_point = now;
			return;
		}
	}
}

/**
 * @file	time.hpp
 * @brief 
 * @author	ningbf
 * @version 1.0.0
 * @date	2023-03-21
 */
#ifndef  _TIME_HPP_
#define  _TIME_HPP_ 

#include <chrono> 
#include <cstdint>

namespace detail{
	class time_unit{
		public:
			static const int64_t	MILLISECONDS_PER_SECOND			= 1000;
			static const int64_t	MICROSECONDS_PER_MILLISECOND	= 1000;
			static const int64_t	NANOSECONDS_PER_MICROSECOND		= 1000;
		
			static const int64_t	MICROSECONDS_PER_SECOND			= MILLISECONDS_PER_SECOND * MICROSECONDS_PER_MILLISECOND;	
			static const int64_t	NANOSECONDS_PER_MILLISECOND		= NANOSECONDS_PER_MICROSECOND * MICROSECONDS_PER_MILLISECOND;
			static const int64_t	NANOSECONDS_PER_SECOND			= NANOSECONDS_PER_MICROSECOND * MICROSECONDS_PER_SECOND; 
			
			static const int64_t	SECONDS_PER_MINUTE				= 60;
			static const int64_t	MINUTES_PER_HOUR				= 60;
			static const int64_t	HOURS_PER_DAY					= 24;
			static const int64_t	SECONDS_PER_HOUR				= SECONDS_PER_MINUTE * MINUTES_PER_HOUR;
			
			static const int64_t	MILLISECONDS_PER_MINUTE			= MILLISECONDS_PER_SECOND * SECONDS_PER_MINUTE;
			static const int64_t	NANOSECONDS_PER_MINUTE			= NANOSECONDS_PER_SECOND  * SECONDS_PER_MINUTE;
			static const int64_t	NANOSECONDS_PER_HOUR			= NANOSECONDS_PER_MINUTE  * MINUTES_PER_HOUR;
			static const int64_t	NANOSECONDS_PER_DAY				= NANOSECONDS_PER_HOUR	  * HOURS_PER_DAY;
	};

	class time 
	{
		private:
			time();
			static time&  get_instance();
		public:
			/**
			 * @reture current nano time int int64_t(unix-timestamp * 1e-9 + nano-part)
			 */
			static int64_t 	now();
			
			static  void	reset();
			
#ifndef _WIN32 
			static	inline  uint64_t  rdtsc(void){
				uint64_t hi, lo;
				__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
				return  lo | (hi << 32);
			}
#endif 
		private:
			int64_t	start_time_since_epoch_;
			int64_t	start_time_steady_;
	};

}

#endif //_TIME_HPP_ 


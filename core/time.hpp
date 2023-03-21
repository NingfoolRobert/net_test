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
class time 
{
	public:
	private:
		time();
		static time&  get_instance();
	public:
		static int64_t 	now();
	private:
		int64_t	start_time_since_epoch_;
		int64_t	start_time_steady_;
};

}

#endif //_TIME_HPP_ 


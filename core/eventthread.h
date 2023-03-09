/**
 * @file	eventthread.h
 * @brief 
 * @author	ningbf
 * @version 1.0.0
 * @date	2023-03-09
 */
#ifndef _EVENT_THREAD_H_
#define _EVENT_THREAD_H_ 


#include "eventloop.h"
#include <thread>

class eventthread{
	public:
		eventthread();
		~eventthread();
	public:
		const eventloop*  get_loop() { return _loop; }	
		
		void	run();

		void	stop();
	private:
		eventloop*								_loop;
		size_t									_flag;
		std::unique_ptr<std::thread>			_thr;
};


#endif //_EVENT_THREAD_H_

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
		const eventloop*  loop() { return _loop; }	
		
		void	run(size_t  timeout = 10);

		void	stop();

		int		get_tid();
	private:
		eventloop*								_loop;
		std::unique_ptr<std::thread>			_thr;
		std::thread::id							_tid;
};


#endif //_EVENT_THREAD_H_

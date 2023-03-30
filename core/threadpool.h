/**
 * @file	threadpool.h
 * @brief 
 * @author	ningbf
 * @version 1.0.0
 * @date	2023-03-09
 */

#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_ 

#include "eventloop.h"
#include "eventthread.h"
#include <atomic>
class threadpool{
	public:
		thread_pool();
		~thread_pool();
	public:
		void	run(int thread_size = 2, int timeout = 10);	
		void	stop();
		size_t	size(){return _ths.size();}
		size_t	get_idx() { return _idx++; }
		const eventloop*  get_loop(int idx) { return _ths[idx % size()].loop(); }
	private:
		std::vector<std::unique_ptr<eventthread>>		_ths;	
		std::atomic_uint32_t			_idx;
};

#endif //_THREAD_POOL_H_


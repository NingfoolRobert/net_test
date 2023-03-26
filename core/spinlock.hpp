/**
 * @file	spinlock.hpp
 * @brief 
 * @author	ningbf
 * @version 1.0.0
 * @date	2023-03-08
 */
#ifndef _SPIN_LOCK_HPP_
#define _SPIN_LOCK_HPP_ 

#include <atomic> 
#include <exception>
class spinlock{
	public:
		spinlock(){
		}
		~spinlock(){}
		//
		spinlock(const spinlock&)				= delete;
		spinlock& operator=(const spinlock&)	= delete;	
		//
		void lock(){
			while(flag_.test_and_set(std::memory_order_acquire));	
		}
		void unlock(){
			flag_.clear(std::memory_order_release);
		}
	private:
		std::atomic_flag			flag_ = ATOMIC_FLAG_INIT;
};


#endif 


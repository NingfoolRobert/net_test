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
class spinlock{
	public:
		spinlock(){
			flag_ = ATOMIC_FLAG_INIT;
		}
		~spinlock(){}
		//
		spinlock(const spinlock&)				= delete;
		spinlock& operator=(const spinlock&)	= delete;	
		//
		void lock(){
#ifndef _NULLLOCK_
			while(flag_.test_and_set(std::memory_order_acquire));	
#endif //_NULLLOCK_ 
		}
		void unlock(){
#ifndef _NULLLOCK_
			flag_.clear(std::memory_order_release);
#endif //_NULLLOCK_
		}
	private:
		std::atomic_flag			flag_;
};


#endif 


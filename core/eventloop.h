#ifndef _EVENT_LOOP_H_
#define _EVENT_LOOP_H_

#include <set>
#include <list>
#include <functional>
#include <thread>

#include "net_io.h"
#include "spinlock.hpp"
#include "lock_free_queue.hpp"



typedef void(*PTIMERCALLBACK)(void*);
struct timer_info_t {
	int32_t					tid;
	int32_t					count;				//-1: unlimit
	int64_t					gap;				//gap time(nanosecond)
	int64_t					expire;				//expire time (nanosecond)
	PTIMERCALLBACK			cb;					//callback function
	void					*param;				//callback param 
};
typedef std::function<void()> task;
class eventloop
{
public:
	eventloop();
	virtual ~eventloop();
public:
	int		loop(int timeout);

	void	add_timer(timer_info_t&  cb);

	void	remove_timer(unsigned short timer_id);

	void	add_task(task tsk);

	void	stop();

	void	update_event(net_io* conn, int event);

	size_t  net_io_size() { return _conns.size(); }
private:
	bool	queue_in_loop();

	void	wakeup();

	void	create_wakeup_fd();

	void	handle_read();

	void	process_timer();

	void	process_task();
private:
	spinlock							_lck;
	std::set<net_io*>					_conns;
private:
	spinlock							_lck_timer;
	std::list<timer_info_t>				_timers;
	//
// 	spinlock							_lck_task;
// 	std::list<task>						_tasks;

	lock_free_queue<task, 1024 * 128>	_tasks;
	
private:
	std::thread::id		_tid;		
	int					_running;
#ifdef _WIN32
	net_io*								_wake_listen;
	net_io*								_wake_send;
	net_io*								_wake_recv;
#else 
	int									_wake_fd;
	int									_ep;
#endif 
};



#endif


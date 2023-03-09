#ifndef _EVENT_LOOP_H_
#define _EVENT_LOOP_H_

#include <set>
#include <list>
#include <functional>

#include "net_client_base.h"
#include "spinlock.hpp"

typedef void(*PTIMERCALLBACK)(void*);
struct timer_data_t {
	unsigned short			timer_id;
	short					count;				//-1: 
	unsigned int			time_gap;			//ms 
	unsigned long long		timestamp;			//ms
	void*					param;				//callback param 
	PTIMERCALLBACK			cb;
};

class eventloop
{
public:
	eventloop();
	virtual ~eventloop();
public:
	int		loop(int timeout);

	void	add(net_client_base*  conn);

	void	remove(net_client_base* conn);
	
	void	add_timer(timer_data_t  cb);

	void	remove_timer(unsigned short timer_id);

	void	wakeup();

	void	add_task(std::function<void()>& task);
	
private:
	void	create_wakeup_fd();

	void	handle_read();

	void	process_timer();

	void	process_task();
	
	void	remove_all();
private:
	spinlock							_lck;
	std::set<net_client_base*>			_conns;
private:
	spinlock							_lck_timer;
	std::list<timer_data_t>				_timers;
	//
	spinlock							_lck_task;
	std::list<std::function<void()> >	_tasks;

private:
	int		_running;
	
#ifdef _WIN32
	net_client_base*					_wake_listen;
	net_client_base*					_wake_send;
	net_client_base*					_wake_recv;
#else 
	int									_wake_fd;
#endif 
};



#endif


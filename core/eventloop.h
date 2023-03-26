#ifndef _EVENT_LOOP_H_
#define _EVENT_LOOP_H_

#include <set>
#include <list>
#include <functional>
#include <thread>

#include "net_io.h"
#include "spinlock.hpp"

typedef void(*PTIMERCALLBACK)(void*);
struct timer_info_t {
	int32_t					tid;
	int32_t					count;				//-1: unlimit
	uint32_t				gap;				//ms 
	uint32_t				owner;
	int64_t					timestamp;			//ms
	PTIMERCALLBACK			cb;
	void					*param;				//callback param 
};

class eventloop
{
public:
	eventloop();
	virtual ~eventloop();
public:
	int		loop(int timeout);

	void	add_net(net_io*  conn);

	void	remove_net(net_io* conn);
	
	void	add_timer(timer_info_t  cb);

	void	remove_timer(unsigned short timer_id);

	void	wakeup();

	void	add_task(std::function<void()>& task);

	bool	queue_in_loop();
	
	void	stop();
private:
	void	create_wakeup_fd();

	void	handle_read();

	void	process_timer();

	void	process_task();
	
	void	remove_all();
private:
	spinlock							_lck;
	std::set<net_io*>					_conns;
#ifdef _NIO_EPOLL_ 
	int									_ep;
	std::unordered_map<int, net_io*>	_ep_conn;
#endif 
private:
	spinlock							_lck_timer;
	std::list<timer_info_t>				_timers;
	//
	spinlock							_lck_task;
	std::list<std::function<void()> >	_tasks;

private:
	std::thread::id		_tid;		
	int					_running;
#ifdef _WIN32
	net_io*								_wake_listen;
	net_io*								_wake_send;
	net_io*								_wake_recv;
#else 
	int									_wake_fd;
#endif 
};



#endif


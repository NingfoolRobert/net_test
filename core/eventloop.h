#ifndef _EVENT_LOOP_H_
#define _EVENT_LOOP_H_



#include "net_client_base.h"

#include <set>
#include <vector>
#include <mutex>

typedef void(*PTIMERCALLBACK)(void*);
struct tagtimercb {
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
	explicit eventloop(void* core);
	virtual ~eventloop();
public:
	int		loop(std::vector<net_client_base*> _active_conn,  int timeout);

	void	add(net_client_base*  conn);

	void	remove(net_client_base* conn);

	void	add_timer(tagtimercb  cb);

	void	remove_timer(unsigned short timer_id);

	void	wakeup();
private:
	void	create_wakeup_fd();

	void	handle_read();

	void	process_timer();

	void	update_conns();
private:
	std::mutex							_lck;
	std::set<net_client_base*>			_conns;
	std::mutex							_wait_lck;
	std::vector<net_client_base*>		_remove_conns;
	std::vector<net_client_base*>		_wait_conns;
private:
	std::mutex							_lck_timer;
	std::vector<tagtimercb>				_list_timers;
private:
	bool		_stop_flag;
	
#ifdef _WIN32
	net_client_base*					_wake_listen_fd;
	net_client_base*					_wake_send_fd;
	net_client_base*					_wake_recv_fd;
#else 
	int									_wake_fd;
#endif 
public:
	void				*_core;
};



#endif


#ifndef _EVENT_LOOP_H_
#define _EVENT_LOOP_H_



#include "net_client_base.h"

#include <set>
#include <vector>
#include <mutex>

typedef void(*PTIMERCALLBACK)(void*);
struct tagtimercb {
	unsigned int			time_gap;			//ms 
	unsigned int			count;				// -1: 
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
	int		loop(std::vector<net_client_base*> _active_conn,  int timeout);

	void	add(net_client_base*  conn);

	void	process_timer();

	void	remove(net_client_base* conn);

	void	add_timer(tagtimercb  cb);

	void	notify_all();
private:
	void	create_wakeup_fd();

	void	handle_read();
private:
	std::mutex							_lck;
	std::set<net_client_base*>			_conns;
private:
	std::mutex							_lck_timer;
	std::vector<tagtimercb>				_list_timers;
private:
	bool		_stop_flag;
	
#if _WIN32
	net_client_base*					_wake_listen_fd;
	net_client_base*					_wake_send_fd;
	net_client_base*					_wake_recv_fd;
#else 
	int									_wake_fd;
#endif 
};



#endif


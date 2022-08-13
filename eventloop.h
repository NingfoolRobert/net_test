#ifndef _EVENT_LOOP_H_
#define _EVENT_LOOP_H_



#include "net_client_base.h"

#include <set>
#include <vector>
#include <mutex>

class eventloop
{
public:
	eventloop();
	virtual ~eventloop();
public:

	int		loop(std::vector<net_client_base*> _active_conn,  int timeout);

	void	add(net_client_base*  conn);

	void	remove(net_client_base* conn);
public:
	std::mutex							_lck;
	std::set<net_client_base*>			_conns;
};



#endif


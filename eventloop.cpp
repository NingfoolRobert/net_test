#include "pch.h"
#include "eventloop.h"

#include <Sockets.h>


eventloop::eventloop()
{
}


eventloop::~eventloop()
{
}

int eventloop::loop(std::vector<net_client_base*> _active_conn, int timeout)
{
	std::vector<net_client_base*> conns;
	fd_set  rd_fds;
	fd_set  wt_fds;
	SOCKET	_max_fd; 

	{
		std::unique_lock<std::mutex> _(_lck);
		for (auto it = _conns.begin(); it != _conns.end(); ++it)
		{
			auto pConn = *it;
			conns.push_back(pConn);
			FD_SET(pConn->m_fd, &rd_fds);
			if (pConn->GetWaitSendCnt())
				FD_SET(pConn->m_fd, &wt_fds);
		}
	}

	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = timeout % 1000;
	int nret = select(_max_fd, &rd_fds, &wt_fds, nullptr, &tv);
	if (nret <= 0)
		return 0;
	//
	for (auto it = conns.begin(); it != conns.end(); ++it)
	{
		auto pConn = *it;
		if (FD_ISSET(pConn->m_fd, &rd_fds))
			pConn->OnRead();
		if (FD_ISSET(pConn->m_fd, &wt_fds))
			pConn->OnSend();
	}
}

void eventloop::add(net_client_base* conn)
{
	std::unique_lock<std::mutex> _(_lck);
	auto it = _conns.find(conn);
	if (it == _conns.end())
		_conns.insert(conn);
}

void eventloop::remove(net_client_base* conn)
{
	std::unique_lock<std::mutex> _(_lck);
	auto it = _conns.find(conn);
	if (it == _conns.end())
		return;
	_conns.erase(conn);
}



#include "eventloop.h"
#include "ngx_log.h"
#include "net_client_base.h"
#include "ngx_core.h"

#if _WIN32
#include <sys/timeb.h>
#include <ws2tcpip.h>

#else
#include <unistd.h>
#include <sys/select.h>
#endif 




eventloop::eventloop(void* core) :_core(core)
{
#ifdef _WIN32
	_wake_recv_fd = NULL;
	_wake_listen_fd = NULL;
	_wake_send_fd = NULL;
#else 
	_wake_fd = -1;
#endif 
	create_wakeup_fd();
}

eventloop::~eventloop()
{
	_conns.clear();
	_list_timers.clear();
#ifdef _WIN32
	if (_wake_listen_fd) {
		delete _wake_listen_fd;
		_wake_listen_fd = NULL;
	}
	if (_wake_recv_fd) {
		delete _wake_recv_fd;
		_wake_recv_fd = NULL;
	}
	if (_wake_send_fd) {
		delete _wake_send_fd;
		_wake_send_fd = NULL;
	}
#else 
	close(_wake_fd);
	_wake_fd = -1;
#endif 
}

int eventloop::loop(std::vector<net_client_base*> _active_conn, int timeout)
{
	process_timer();
	//
	int max_fd = 0;
	std::vector<net_client_base*> conns;
	fd_set  rd_fds;
	fd_set  wt_fds;
	SOCKET	_max_fd; 
#ifdef _WIN32 
	FD_SET(_wake_recv_fd->_fd, &rd_fds);
#else
	FD_SET(_wake_fd, &rd_fds);
	max_fd = max(_wake_fd, max_fd);
#endif 
	{
		std::unique_lock<std::mutex> _(_lck);
		for (auto it = _conns.begin(); it != _conns.end(); ++it)
		{
			auto pConn = *it;
			conns.push_back(pConn);
			FD_SET(pConn->_fd, &rd_fds);
#ifndef _WIN32 
			max_fd = max(pConn->_fd, max_fd);
#endif 
			if (pConn->get_wait_send_cnt())
				FD_SET(pConn->_fd, &wt_fds);
		}
	}
	//
	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	int nret = select(max_fd + 1, &rd_fds, &wt_fds, nullptr, &tv);
	//
	for (auto it = conns.begin(); it != conns.end(); ++it)
	{
		auto pConn = *it;
		if (FD_ISSET(pConn->_fd, &rd_fds))
			pConn->OnRead();
		if (FD_ISSET(pConn->_fd, &wt_fds))
			pConn->OnSend();
	}

#ifdef _WIN32 
	if (FD_ISSET(_wake_recv_fd->_fd, &rd_fds))
#else 
	if(FD_ISSET(_wake_fd, &rd_fds))
#endif 
		handle_read();
	return 0;
}

void eventloop::add(net_client_base* conn)
{
	std::unique_lock<std::mutex> _(_lck);
	auto it = _conns.find(conn);
	if (it == _conns.end())
		_conns.insert(conn);
}

void eventloop::process_timer()
{
	unsigned long long	now = 0;
#if _WIN32
	timeb tbnow;
	ftime(&tbnow);
	now = tbnow.time * 1000 + tbnow.millitm;
#else 
	timeval tv;
	get_time_of(&tv);
	now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
	//
	std::unique_lock<std::mutex> _(_lck_timer);
	auto it = _list_timers.begin();
	while (it != _list_timers.end())
	{
		tagtimercb timer = *it;
		if (timer.timestamp + timer.time_gap > now)
		{
			timer.cb(timer.param);
			timer.timestamp = now;
		}
		//
		if (timer.count >= 0)
		{
			if (--timer.count == 0)
			{
				it = _list_timers.erase(it);
				continue;
			}
		}
		//
		++it;
	}
}

void eventloop::remove(net_client_base* conn)
{
	std::unique_lock<std::mutex> _(_lck);
	auto it = _conns.find(conn);
	if (it == _conns.end())
		return;
	_conns.erase(conn);
}

void eventloop::add_timer(tagtimercb cb)
{
	std::unique_lock<std::mutex> _(_lck_timer);
	unsigned long long	now = 0;
#if _WIN32
	timeb tbnow;
	ftime(&tbnow);
	now = tbnow.time * 1000 + tbnow.millitm;
#else 
	timeval tv;
	get_time_of(&tv);
	now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
	cb.timestamp = now;
	_list_timers.push_back(cb);
}

void eventloop::remove_timer(unsigned short timer_id)
{
	std::unique_lock<std::mutex> _(_lck_timer);
	auto it = _list_timers.begin();
	while (it != _list_timers.end())
	{
		if (it->timer_id == timer_id)
			it = _list_timers.erase(it);
		else
			++it;
	}
}

void eventloop::wakeup()
{
	uint64_t one = 1;
#ifdef _WIN32
	int len = _wake_send_fd->send_msg((char*)&one, sizeof(one));
#else 
	int len = ::write(_wake_fd, &one, sizeof(one));
#endif 
}

void eventloop::create_wakeup_fd()
{
#ifdef _WIN32 
	_wake_listen_fd = new net_client_base(this, NULL, NULL);
	_wake_listen_fd->create();
	_wake_listen_fd->set_reuse_addr();
	unsigned int host_ip = inet_addr("127.0.0.1");
	_wake_listen_fd->bind(host_ip, 0);
	_wake_listen_fd->listen();
	
	struct sockaddr_in svr_addr;
	int sock_len = sizeof(svr_addr);
	if (getsockname(_wake_listen_fd->_fd, (struct sockaddr*)&svr_addr, &sock_len) < 0)
		return;
	//
	int port = ntohl(svr_addr.sin_port);
	
	_wake_send_fd = new net_client_base(this, NULL, NULL);
	_wake_send_fd->create();
	_wake_send_fd->connect(host_ip, port);
	
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	_wake_recv_fd = new net_client_base(this, NULL, NULL);
	_wake_recv_fd->_fd = ::accept(_wake_listen_fd->_fd, (struct sockaddr*)&client_addr, &client_addr_len);
	
	_wake_recv_fd->set_nio();
	_wake_send_fd->set_nio();
#else 
	_wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#endif 
}

void eventloop::handle_read()
{
	char szTmp[32] = { 0 };
#ifdef _WIN32 
	int readed = ::recv(_wake_recv_fd->_fd, szTmp, 32, 0);
#else 
	int readed = ::read(_wake_fd, szTmp, 32);
#endif
}

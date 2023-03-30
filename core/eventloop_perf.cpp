#if ((defined(__GNUC__) && !defined(_EPOLL_)) || (defined(_WIN32))) && !defined(_BUSY_LOOP_)
#ifdef _WIN32 
#include <sys/timeb.h>
#include <ws2tcpip.h>
#else 
#include <unistd.h>
#include <sys/select.h>
#endif 
#include "eventloop.h"
#include "time.hpp"
#include <mutex>
#include <functional>

//
int eventloop::loop(int timeout) {
	_tid = std::this_thread::get_id();
	int max_fd = 0;
	fd_set rd_fds, wt_fds;

	struct timeval tv { 0, 0 };
	while (_running)
	{
		process_task();
		process_timer();
		//
		FD_ZERO(&rd_fds);
		FD_ZERO(&wt_fds);
#ifndef _WIN32 
		FD_SET(_wake_fd, &rd_fds);
#else 
		FD_SET(_wake_recv->_fd, &rd_fds);
#endif 
		{
			std::unique_lock<spinlock> _(_lck);
			for (auto it = _conns.begin(); it != _conns.end(); ++it)
			{
				auto conn = *it;
				if (conn->_ev & EV_READ)
					FD_SET(conn->_fd, &rd_fds);
				if (conn->_ev & EV_WRITE)
					FD_SET(conn->_fd, &wt_fds);
#ifndef _WIN32 
				max_fd = std::max(max_fd, conn->_fd);
#endif 
			}
		}
		//
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000 ) * 1000;
		int ret = select(max_fd + 1, &rd_fds, &wt_fds, NULL, &tv);
		if (ret <= 0)
			continue;
#ifndef _WIN32 
		if (FD_ISSET(_wake_fd, &rd_fds))
#else 
		if (FD_ISSET(_wake_recv->_fd, &rd_fds))
#endif 
			handle_read();
		//
		{
			std::unique_lock<spinlock> _(_lck);
			for (auto it = _conns.begin(); it != _conns.end(); ++it)
			{
				auto conn = *it;
				if (FD_ISSET(conn->_fd, &rd_fds))
					conn->OnRecv();
				if (FD_ISSET(conn->_fd, &wt_fds))
					conn->OnSend();
			}
		}
	}
	//
	{
		std::unique_lock<spinlock> _(_lck);
		for (auto it = _conns.begin(); it != _conns.end(); ++it) {
			auto conn = *it;
			conn->terminate();
		}
	}
	//
	process_task();
	return 0;
}
//
void eventloop::update_event(net_io* conn, int ev) {
	if (nullptr == conn || ev == conn->_ev) 
		return;
	//
	auto ev_new = conn->_ev & (EV_READ | EV_WRITE);
	if (ev_new == EV_NULL && ev != EV_DELETED) { //add
		conn->add_ref();
		add_task(std::bind([this, conn, ev]() {
			std::unique_lock<spinlock> _(_lck);
			auto it = _conns.find(conn);
			if (it == _conns.end()) {
				conn->add_ref();
				conn->set_nio();
				conn->_loop = this;
				_conns.insert(conn);
			}
			conn->_ev = ev;
			conn->release();
		}));
		return;
	}
	//
	
	if (!queue_in_loop())	{
		conn->add_ref();
		add_task(std::bind([this, conn, ev]() {
			update_event(conn, ev);
			conn->release();
		}));
		return;
	}

	if (ev == EV_DELETED){	//deleted 
		conn->add_ref();
		add_task(std::bind([this, conn]() {
			std::unique_lock<spinlock> _(_lck);
			auto it = _conns.find(conn);
			if (it != _conns.end()) {
				auto pOld = *it;
				pOld->_loop = NULL;
				pOld->release();
				_conns.erase(it);
			}
			conn->release();
		}));
	}
	//update event
	conn->_ev = ev;
}

#endif //Base

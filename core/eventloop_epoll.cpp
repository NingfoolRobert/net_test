#if defined(_EPOLL_) && defined(__GNUC__)
#include "eventloop.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <mutex>
#include <vector>

//
int eventloop::loop(int timeout) {
	_tid = std::this_thread::get_id();
	_ep = epoll_create(1024);
	if(_ep == -1){
		return errno;
	}
	//	
	struct epoll_event evt;
	evt.data.fd = _wake_fd;
	evt.events = EPOLLIN;
	epoll_ctl(_ep, EPOLL_CTL_ADD, _wake_fd, &evt);
	std::vector<struct epoll_event> vecTmp;
	while (_running)
	{
		process_task();
		process_timer();
		//
		vecTmp.resize(_conns.size() + 1);
		//
		int ret = epoll_wait(_ep, &vecTmp[0], vecTmp.size(), timeout);
		for (auto i = 0; i < ret; ++i) {
			auto& ev = vecTmp[i];
			if (ev.data.fd == _wake_fd) {
				handle_read();
			}
			else {
				auto conn = reinterpret_cast<net_io*>(ev.data.ptr);
				if (ev.events & (EPOLLIN | EPOLLHUP | EPOLLERR))
					conn->OnRecv();
				if (ev.events & EPOLLOUT)
					conn->OnSend();
			}
			
		}
		vecTmp.clear();
		//
	}
	return true;
}
//
void eventloop::update_event(net_io* conn, int ev) {
	if (nullptr == conn || ev == conn->_ev) 
		return;
	//
	if (!queue_in_loop()) {
		conn->add_ref();
		add_task(std::bind([this, conn, ev]() {
			update_event(conn, ev);
			conn->release();
		}));
		return;
	}
	//
	struct epoll_event evt;
	if (ev == EV_DELETED){
		evt.data.ptr = conn;
		epoll_ctl(_ep, EPOLL_CTL_DEL, conn->_fd, &evt);
		conn->_ev = EV_DELETED;
		//
		add_task(std::bind([this, conn]{
		std::unique_lock<spinlock> _(_lck);
		auto it = _conns.find(conn);
		if (it != _conns.end()) {
			auto old = *it;
			old->release();
			_conns.erase(it);
		}
		conn->release();
		}));
		//
	}
	else {
		//
		evt.data.ptr = conn;
		evt.events = 0;
		if (ev & EV_READ)
			evt.events |= EPOLLIN;
		if (ev & EV_WRITE)
			evt.events |= EPOLLOUT;
		//
		if (EV_NULL == conn->_ev) {
			//
			conn->add_ref();
			epoll_ctl(_ep, EPOLL_CTL_ADD, conn->_fd, &evt);
			std::unique_lock<spinlock> _(_lck);
			auto it = _conns.find(conn);
			if (it == _conns.end()) {
				_conns.insert(conn);
				conn->add_ref();
				conn->_loop = this;
			}
		}
		else {
			epoll_ctl(_ep, EPOLL_CTL_MOD, conn->_fd, &evt);
		}
	}
	//
	conn->_ev = ev;
}

#endif 

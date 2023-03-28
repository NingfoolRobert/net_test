#if defined(_GNUC_) && defined(_EPOLL_)
#include "eventloop.h"
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#include <functional>
#include <mutex>
#include <vector>

// void eventloop::add_net(net_io* conn) {
// 
// 	if (nullptr == conn)
// 		return;
// 	//
// 	if (!queue_in_loop())
// 	{
// 		add_task(std::bind(&eventloop::add_net, this, conn));
// 		return;
// 	}
// 	//
// 	auto it = _conns.find(conn);
// 	if (it == _conns.end()) {
// 		_conns.insert(conn);
// 	}
// }
// //
// void eventloop::remove_net(net_io* conn) {
// 	if (nullptr == conn)
// 		return;
// 	//
// 	if (!queue_in_loop())
// 	{
// 		add_task(std::bind(&eventloop::remove_net, this, conn));
// 		return;
// 	}
// 	//
// 	auto it = _conns.find(conn);
// 	if (it != _conns.end()){
// 		auto pOld = *it;
// 		pOld->release();
// 		_conns.erase(it);
// 	}
// }
//
int eventloop::loop(int timeout) {
	_tid = std::this_thread::get_id();
	int max_fd = 0;
	struct epoll_event evt;
	evt.data.fd = _wake_fd;
	evt.event = EPOLLIN | EPOLLLT;
	epoll_ctl(_ep, _wake_fd, EPOLL_CTL_ADD, &evt);
	std::vector<struct epoll_event> vecTmp;
	struct timeval tv { 0, 0 };
	while (_running)
	{
		process_task();
		process_timer();
		//
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000 ) * 1000;
		vecTmp.resize(_conns.size() + 1);
		//
		int ret = epoll_wait(_ep, &vecTmp[0], &tv);
		for (auto i = 0u; i < ret; ++i) {
			auto& ev = vecTmp[i];
			if (ev.data.fd == _wake_fd) {
				handle_read();
			}
			else {
				auto conn = ev.data.ptr;
				if (ev.event & (EPOLLIN | EPOLLHUP | EPOLLERR))
					conn->OnRecv();
				if (ev.event & EPOLLOUT)
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
		epoll_ctl(_ep, EPOLL_CTL_DEL, conn->_fd, &evt);
		std::unique_lock<spinlock> _(_lck);
			auto it = _conns.find(conn);
			if (it != _conns.end()) {
				auto old = *it;
				old->release();
				_conns.erase(it);
			}
		}));
		conn->release();
	}
	else {
		//
		evt.data.ptr = conn;
		if (ev & EV_READ)
			evt.event |= EPOLLIN;
		if (ev & EV_WRITE)
			evt.event |= EPOLLOUT;
		//
		if (EV_NULL == conn->_ev) {
			//
			conn->add_ref();
			epoll_ctl(_ep, EPOLL_CTL_ADD, con->_fd, &evt);
			std::unique_lock<spinlock> _(_lck);
			auto it = _conns.find(conn);
			if (it != _conns.end()) {
				_conns.insert(conn);
				conn->add_ref();
			}
		}
		else {
			epoll_ctl(_ep, EPOLL_CTL_MOD, con->_fd, &evt);
		}
	}
	//
	conn->_ev = ev;
}

#endif 
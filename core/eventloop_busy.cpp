#ifdef _BUSY_LOOP_
#include "eventloop.h"
#include <mutex>
#include <functional>
//
int eventloop::loop(int timeout) {
	_tid = std::this_thread::get_id();
	while (_running)
	{
		process_task();
		process_timer();
		handle_read();
		//
		{
			std::unique_lock<spinlock> _(_lck);
			for (auto it = _conns.begin(); it != _conns.end(); ++it)
			{
				auto conn = *it;
				if (conn->_ev & EV_READ)
					conn->OnRecv();
				if (conn->_ev & EV_WRITE)
					conn->OnSend();
			}
		}

	}
	return true;
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
				conn->_ev = ev;
			}
			conn->release();
		}));
		return;
	}
	//

	if (!queue_in_loop()) {
		conn->add_ref();
		add_task(std::bind([this, conn, ev]() {
			update_event(conn, ev);
			conn->release();
		}));
		return;
	}

	if (ev == EV_DELETED) {	//deleted 
		//
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
	//
	conn->_ev = ev;
}

#endif //_BUSY_LOOP_

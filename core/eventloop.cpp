#include "eventloop.h"
#include "log_def.h"
#include <vector>

#ifdef  _WIN32
#include <sys/timeb.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/eventfd.h>
#endif 


eventloop::eventloop():_running(true)
{
#ifdef _WIN32
	_wake_recv = NULL;
	_wake_listen = NULL;
	_wake_send = NULL;
	//
	WSAData wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
		return;
#else 
	_wake_fd = -1;
#endif 
	create_wakeup_fd();
}

eventloop::~eventloop()
{
	_running = false;
	remove_all();
	//
	_timers.clear();
#ifdef _WIN32
	if (_wake_listen) {
		delete _wake_listen;
		_wake_listen = NULL;
	}
	if (_wake_recv) {
		delete _wake_recv;
		_wake_recv = NULL;
	}
	if (_wake_send) {
		delete _wake_send;
		_wake_send = NULL;
	}
	//
	WSACleanup();
#else 
	if (_wake_fd != -1){
		close(_wake_fd);
		_wake_fd = -1;
	}
#endif 
}

int eventloop::loop(int timeout)
{
	process_timer();
	//
	int max_fd = 0;
	std::vector<net_client_base*> conns;
	fd_set  rd_fds, wt_fds;
	FD_ZERO(&rd_fds);
	FD_ZERO(&wt_fds);
#ifdef _WIN32 
	FD_SET(_wake_recv->_fd, &rd_fds);
#else
	FD_SET(_wake_fd, &rd_fds);
	max_fd = std::max(_wake_fd, max_fd);
#endif 
	{
		std::unique_lock<std::mutex> _(_lck);
		for (auto it = _conns.begin(); it != _conns.end(); ++it)
		{
			auto pConn = *it;
			conns.push_back(pConn); pConn->add_ref();
			FD_SET(pConn->_fd, &rd_fds);
#ifndef _WIN32 
			max_fd = std::max(pConn->_fd, max_fd);
#endif 
			if (pConn->wait_sndmsg_size())
				FD_SET(pConn->_fd, &wt_fds);
		}
	}
	//
	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	int nret = select(max_fd + 1, &rd_fds, &wt_fds, nullptr, &tv);
	if (nret <= 0)
		return 0;
	//
#ifdef _WIN32 
	if (FD_ISSET(_wake_recv->_fd, &rd_fds))
#else 
	if (FD_ISSET(_wake_fd, &rd_fds))
#endif 
	{
		handle_read();
	}
	//
	for (auto i = 0u; i < conns.size(); ++i)
	{
		auto pConn = conns[i];
		if (FD_ISSET(pConn->_fd, &rd_fds))
			pConn->OnRead();
		if (FD_ISSET(pConn->_fd, &wt_fds))
			pConn->OnSend();
		//
		pConn->release();
	}

	return nret; 
}

void eventloop::add(net_client_base* conn)
{
	if(!_running || nullptr == conn) 
		return ;
	{
		std::unique_lock<std::mutex> _(_lck);
		conn->_loop = this;
		_conns.insert(conn);
	}
	//
	wakeup();
}

void eventloop::process_timer()
{
	unsigned long long	now = 0;
#if _WIN32
	timeb tbnow;
	ftime(&tbnow);
	now = tbnow.time * 1000 + tbnow.millitm;
#else 
	struct timeval tv;
	gettimeofday(&tv, NULL);
	now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
	//
	std::vector<timer_data_t> vecTmp;
	{
		std::unique_lock<std::mutex> _(_lck_timer);
		auto it = _timers.begin();
		while (it != _timers.end())
		{
			timer_data_t t = *it;
			if (t.timestamp >= now)
			{
				vecTmp.push_back(t);
				if (t.count > 0)
					t.count--;
				//
				if (t.count == 0) 
					_timers.erase(it);
				else 
					t.timestamp += t.time_gap;
			}
		}
	}
	//
	for(auto i = 0u; i < vecTmp.size(); i++)
	{
		timer_data_t& timer = vecTmp[i];
		timer.cb(timer.param);
	}
}

void eventloop::remove(net_client_base* conn)
{
	if(NULL == conn) 
		return ;
	{
		std::unique_lock<std::mutex> _(_lck);
		auto it = _conns.find(conn);
		if (it != _conns.end())
			_conns.erase(it);
	}
	//
	wakeup();
}

void eventloop::remove_all()
{
	//
	{
		std::unique_lock<std::mutex> _(_lck);
		for(auto it = _conns.begin();  it != _conns.end(); ++it)
		{
			auto pConn = *it;
			pConn->release();		
		}
		_conns.clear();
	}
	//
// 	std::unique_lock<std::mutex> _(_lck);
// 	for(auto i = 0u; i < _wait_conns.size(); ++i)	
// 	{
// 		auto pConn = _wait_conns[i];
// 		if(pConn == NULL)
// 			continue;
// 		
// 		pConn->OnClose();		
// 		pConn->release();
// 	}
// 	//
// 	_wait_conns.clear();
// 	for(auto i = 0u; i < _remove_conns.size(); ++i)
// 	{
// 		auto pConn = _remove_conns[i];
// 		pConn->release();
// 	}
// 	//	
// 	_remove_conns.clear();
}


void eventloop::add_timer(timer_data_t  cb)
{
	std::unique_lock<std::mutex> _(_lck_timer);
	if (_running) return;
	unsigned long long	now = 0;
#if _WIN32
	timeb tbnow;
	ftime(&tbnow);
	now = tbnow.time * 1000 + tbnow.millitm;
#else 
	timeval tv;
	gettimeofday(&tv, NULL);
	now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
	cb.timestamp = now;
	_timers.push_back(cb);
}

void eventloop::remove_timer(unsigned short timer_id)
{
	std::unique_lock<std::mutex> _(_lck_timer);
	auto it = _timers.begin();
	while (it != _timers.end())
	{
		if (it->timer_id == timer_id)
			it = _timers.erase(it);
		else
			++it;
	}
}

void eventloop::wakeup()
{
	if (!_running)
		return;
	uint64_t one = 1;
#ifdef _WIN32
	if (_wake_send) {
		int len = _wake_send->send((char*)&one, sizeof(one));
#else 
	if(_wake_fd != -1){
	int len = ::write(_wake_fd, &one, sizeof(one));
#endif 
	if (len < (int)sizeof(one))
		error_print("%s, send data len < sizeof(uint64_t)", __FUNCTION__);
	}
}
	
void eventloop::create_wakeup_fd()
{
#ifdef _WIN32 
	_wake_listen = new net_client_base(NULL, NULL);
	_wake_listen->_loop = this;
	_wake_listen->create();
	_wake_listen->set_reuse_addr();
	unsigned int host_ip = ntohl(inet_addr("127.0.0.1"));
	_wake_listen->bind(host_ip, 0);
	_wake_listen->listen();
	
	struct sockaddr_in svr_addr;
	int sock_len = sizeof(svr_addr);
	int ret = getsockname(_wake_listen->_fd, (struct sockaddr*)&svr_addr, &sock_len);
	if (ret < 0)
	{
		int err = GetLastError();
		return;
	}
	//
	int port = ntohs(svr_addr.sin_port);
	
	_wake_send = new net_client_base(NULL, NULL);
	_wake_send->create();
	_wake_send->connect(host_ip, port);
	
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	_wake_recv = new net_client_base(NULL, NULL);
	_wake_recv->_fd = ::accept(_wake_listen->_fd, (struct sockaddr*)&client_addr, &client_addr_len);
	
	_wake_recv->set_nio();
	_wake_send->set_nio();
#else 
	_wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#endif 
}

void eventloop::handle_read()
{
	char szTmp[32] = { 0 };
#ifdef _WIN32 
	int readed = _wake_recv->recv(szTmp, 32);
	if(readed < (int)sizeof(int))
#else 
	int readed = ::read(_wake_fd, szTmp, 32);
	if (readed < (int)sizeof(uint64_t))
#endif
		printf("%s, recv data < sizeof(uint64_t)\n", __FUNCTION__);
}

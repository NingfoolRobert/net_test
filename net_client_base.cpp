#include "net_client_base.h"
#include "eventloop.h"

#ifdef _WIN32
#include <sys/timeb.h>
#include <ws2tcpip.h>
#pragma  comment(lib, "ws2_32.lib")
#endif 

net_client_base::net_client_base(eventloop* loop, PNETMSGCALLBACK fnc, PDISCONNCALLBACK dis_conn_cb):
	_break_timestamp(0),
	_msg_cb(fnc),
	_dis_conn_cb(dis_conn_cb)
{
#ifdef _WIN32 
	_fd = INVALID_SOCKET;
#else 
	_fd = -1;
#endif 
}

net_client_base::~net_client_base()
{
	close();
}

#ifdef _WIN32 
SOCKET net_client_base::create(int domain /*= AF_INET*/, int socket_type /*= SOCK_STREAM*/, int protocol_type /*= IPPROTO_IP*/)
#else 
int	 net_client_base::create(int domain /*= AF_INET*/, int socket_type /*= SOCK_STREAM*/, int protocol_type /*= IPPROTO_IP*/)
#endif 
{
	_fd = ::socket(domain, socket_type, protocol_type);
	return _fd;
}

bool net_client_base::connect(unsigned int host_ip, unsigned short port)
{
	struct sockaddr_in  svr_addr;
	memset(&svr_addr, 0, sizeof(struct sockaddr_in));
	
	svr_addr.sin_family = AF_INET;
#ifdef _WIN32 
	svr_addr.sin_addr.S_un.S_addr = htonl(host_ip);
#else 
	svr_addr.sin_addr.s_addr = htonl(host_ip);
#endif
	svr_addr.sin_port = htons(port);
	if (::connect(_fd, (struct sockaddr*)&svr_addr, sizeof(svr_addr)) < 0)
	{
		return false;
	}
	//
	_break_timestamp = 0;
	_ip = host_ip;
	_port = port;
	return true;
}

bool net_client_base::bind(unsigned int host_ip, unsigned short port)
{
	struct sockaddr_in  bind_addr;
	bind_addr.sin_family = AF_INET;
#ifdef _WIN32 
	bind_addr.sin_addr.S_un.S_addr = htonl(host_ip);
#else 
	bind_addr.sin_addr.s_addr = htonl(host_ip);
#endif
	bind_addr.sin_port = htons(port);
	
	if (::bind(_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0)
	{
		return false;
	}
	_ip = host_ip;
	_port = port;
	return true;
}

bool net_client_base::listen(int backlog /*= 10*/)
{
	if (::listen(_fd, backlog) < 0)
		return false;
	return true;
}

bool net_client_base::set_tcp_nodelay()
{
	int enable = 1;
	setsockopt(_fd, IPPROTO_IP, TCP_NODELAY, (char*)&enable, sizeof(enable));
	return true;
}

bool net_client_base::set_nio(int mode /*= 1*/)
{
#ifdef _WIN32
	ioctlsocket(_fd, FIONBIO, (u_long*)&mode);
#else
	int flags = fcntl(_fd, F_GETFL, 0);
	fcntl(_fd, F_SETFL, mode == 1? O_NONBLOCK | flags : ~O_NONBLOCK & flags)
#endif 
	return true;
}


bool net_client_base::set_reuse_addr(bool flag /*= 1*/)
{
	int opt = flag ? 1 : 0;
#ifdef _WIN32 
	::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else 
	::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, static_cast<socklen_t>(sizeof(opt)));
#endif 
	return true;
}

bool net_client_base::set_reuse_port(bool flag /*= 1*/)
{
#ifndef _WIN32 
	int opt = flag ? 1 : 0;
	::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &opt, static_cast<socklen_t>(sizeof(opt)));
#endif 
	return true;
}

void net_client_base::close()
{
#ifdef _WIN32
	closesocket(_fd);
	_fd = INVALID_SOCKET;
#else 
	::close(_fd);
	_fd = -1;
#endif 
}

void net_client_base::terminate()
{
	//
	if (_loop)
		_loop->remove(this);
	_break_timestamp = time(NULL);
}

int net_client_base::send_msg(const char* pData, unsigned int nMsgLen)
{
	if (_break_timestamp)
		return 0;
	//
	int sended_len = ::send(_fd, pData, nMsgLen, 0);
	return sended_len;
}

void net_client_base::OnTerminate()
{
	if (_break_timestamp)
		return;
	//
	close();
	//
	_break_timestamp = time(NULL);
	//
	if (_dis_conn_cb)
		(*_dis_conn_cb)(_loop->_core, this);
}

bool net_client_base::OnMessage(void* data, unsigned int len)
{
	if (_msg_cb)
		return (*_msg_cb)(_loop->_core, data, len);
	return false;
}


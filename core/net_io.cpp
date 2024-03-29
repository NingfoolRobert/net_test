#include "net_io.h"
#include "eventloop.h"

#include <string.h>
#ifdef _WIN32
#include <sys/timeb.h>
#include <ws2tcpip.h>
#pragma  comment(lib, "ws2_32.lib")
#else 
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif 

net_io::net_io(PNETMSGCALLBACK fnc, PDISCONNCALLBACK disconn_cb):
	_brk_tm(0),
	_errno(0),
	_loop(NULL),
	_ip(0),
	_port(0),
	_msg_cb(fnc),
	_disconn_cb(disconn_cb)
{
#ifdef _WIN32 
	_fd = INVALID_SOCKET;
#else 
	_fd = -1;
#endif 
	_ref = 1;
}

net_io::~net_io()
{
	terminate();
}

ngx_sock net_io::create(int domain /*= AF_INET*/, int socket_type /*= SOCK_STREAM*/, int protocol_type /*= IPPROTO_IP*/)
{
	_fd = ::socket(domain, socket_type, protocol_type);
#ifdef _WIN32
	if (_fd == INVALID_SOCKET)
		_errno = GetLastError();
#else 
	if (_fd == -1)
		_errno = errno;
#endif 
	_errno = 0;
	return _fd;
}

bool net_io::connect(unsigned int host_ip, unsigned short port)
{
	_ip = host_ip;
	_port = port;
	struct sockaddr_in  svr_addr;
	memset(&svr_addr, 0, sizeof(struct sockaddr_in));
	
	svr_addr.sin_family = AF_INET;
#ifdef _WIN32 
	svr_addr.sin_addr.S_un.S_addr = htonl(host_ip);
#else 
	svr_addr.sin_addr.s_addr = htonl(host_ip);
#endif
	svr_addr.sin_port = htons(port);
	int ret = ::connect(_fd, (struct sockaddr*)&svr_addr, sizeof(svr_addr));
	if (ret == -1) {
#ifdef _WIN32 
		_errno = GetLastError();
#else 
		_errno = errno;
#endif 
		return false;
	}
	//
	_brk_tm = 0;
	return true;
}

bool net_io::bind(unsigned int host_ip, unsigned short port)
{
	_ip = host_ip;
	_port = port;
	struct sockaddr_in  bind_addr;
	bind_addr.sin_family = AF_INET;
#ifdef _WIN32 
	bind_addr.sin_addr.S_un.S_addr = htonl(host_ip);
#else 
	bind_addr.sin_addr.s_addr = htonl(host_ip);
#endif
	bind_addr.sin_port = htons(port);
	
	int ret = ::bind(_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)); 
	if (ret == -1)
#ifdef _WIN32 
		_errno = GetLastError();
#else 
		_errno = errno;
#endif 
	return ret == 0;
}

bool net_io::listen(int backlog /*= 10*/)
{
	int ret = ::listen(_fd, backlog);
	if (ret == -1)
#ifdef _WIN32 
		_errno = GetLastError();
#else 
		_errno = errno;
#endif 
	return ret == 0;
}
	
int net_io::send(const char* data, unsigned int len)
{
	return ::send(_fd, data, len, 0);
}

int net_io::recv(char* data, unsigned int len)
{
	return ::recv(_fd, data, len, 0);
}

bool net_io::set_tcp_linger()
{
	struct linger so_linger;
	so_linger.l_onoff = 1;
	so_linger.l_linger = 30;
#ifdef _WIN32 
	return setsockopt(_fd, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(so_linger)) == 0;
#else 
	return setsockopt(_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) == 0;
#endif 
}

bool net_io::set_tcp_nodelay()
{
	int enable = 1;
	return setsockopt(_fd, IPPROTO_IP, TCP_NODELAY, (char*)&enable, sizeof(enable)) == 0;
}

bool net_io::set_nio(int mode /*= 1*/)
{
#ifdef _WIN32
	::ioctlsocket(_fd, FIONBIO, (u_long*)&mode);
#else
	int flags = fcntl(_fd, F_GETFL, 0);
	fcntl(_fd, F_SETFL, mode == 1? O_NONBLOCK | flags : ~O_NONBLOCK & flags);
#endif 
	return true;
}


bool net_io::set_reuse_addr(bool flag /*= 1*/)
{
	int opt = flag ? 1 : 0;
#ifdef _WIN32 
	::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else 
	::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, static_cast<socklen_t>(sizeof(opt)));
#endif 
	return true;
}

bool net_io::set_reuse_port(bool flag /*= 1*/)
{
#ifndef _WIN32 
	int opt = flag ? 1 : 0;
	return ::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &opt, static_cast<socklen_t>(sizeof(opt))) == 0;
#else
	return true;
#endif 
}

void net_io::close()
{
#ifdef _WIN32
	if(_fd != INVALID_SOCKET){
	::closesocket(_fd);
	_fd = INVALID_SOCKET;
#else 
	if(_fd < 0){
	::close(_fd);
	_fd = -1;
#endif 
	}
}

void net_io::terminate()
{
	//
	if (_loop)
	{
		_loop->remove_net(this);
		_loop = NULL;
		OnClose();
	}
	//
	if(_brk_tm)  
		close();
	//
	_brk_tm = time(NULL);
}
//
bool net_io::OnMessage(void* data, unsigned int len)
{
	if (_msg_cb)
		return (*_msg_cb)(this, data, len);
	return false;
}
//
void net_io::OnClose()
{
	//
	if (_disconn_cb)
	{
		(*_disconn_cb)(_errno, this);
		_disconn_cb = NULL;
	}
}

void net_io::get_sock_name()
{
	struct sockaddr_in laddr;
	socklen_t slen = sizeof(laddr);
	getsockname(_fd, (struct sockaddr*)&laddr, &slen);
	
	_ip = ntohl(laddr.sin_addr.s_addr);
	_port = ntohs(laddr.sin_port);
}
	
void net_io::get_peer_name()
{
	struct sockaddr_in paddr;
	socklen_t slen = sizeof(paddr);
	getpeername(_fd, (struct sockaddr*)&paddr, &slen);
	_ip = ntohl(paddr.sin_addr.s_addr);
	_port = ntohs(paddr.sin_port);
}
	
char* net_io::get_ip(char*  ip)
{
	sprintf(ip, "%d.%d.%d.%d", _ip >> 24, (_ip >> 16) & 0xFF, (_ip >> 8) & 0xFF, _ip & 0xFF);
	return ip;
}

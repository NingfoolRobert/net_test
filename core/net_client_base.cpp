#include "net_client_base.h"
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

net_client_base::net_client_base(PNETMSGCALLBACK fnc, PDISCONNCALLBACK disconn_cb):
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

net_client_base::~net_client_base()
{
	close();
}

ngx_sock net_client_base::create(int domain /*= AF_INET*/, int socket_type /*= SOCK_STREAM*/, int protocol_type /*= IPPROTO_IP*/)
{
	_fd = ::socket(domain, socket_type, protocol_type);
	return _fd;
}

bool net_client_base::connect(unsigned int host_ip, unsigned short port)
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
	if (::connect(_fd, (struct sockaddr*)&svr_addr, sizeof(svr_addr)) < 0)
	{
		return false;
	}
	//
	_brk_tm = 0;
	return true;
}

bool net_client_base::bind(unsigned int host_ip, unsigned short port)
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
	
	return ::bind(_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0;
}

bool net_client_base::listen(int backlog /*= 10*/)
{
	return ::listen(_fd, backlog) < 0;
}
	
int net_client_base::send(const char* data, unsigned int len)
{
	return ::send(_fd, data, len, 0);
}

int net_client_base::recv(char* data, unsigned int len)
{
	return ::recv(_fd, data, len, 0);
}

bool net_client_base::set_tcp_linger()
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

bool net_client_base::set_tcp_nodelay()
{
	int enable = 1;
	return setsockopt(_fd, IPPROTO_IP, TCP_NODELAY, (char*)&enable, sizeof(enable)) == 0;
}

bool net_client_base::set_nio(int mode /*= 1*/)
{
#ifdef _WIN32
	::ioctlsocket(_fd, FIONBIO, (u_long*)&mode);
#else
	int flags = fcntl(_fd, F_GETFL, 0);
	fcntl(_fd, F_SETFL, mode == 1? O_NONBLOCK | flags : ~O_NONBLOCK & flags);
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
	return ::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &opt, static_cast<socklen_t>(sizeof(opt))) == 0;
#else
	return true;
#endif 
}

void net_client_base::close()
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

void net_client_base::terminate()
{
	//
	if (_loop)
		_loop->remove(this);
	_brk_tm = time(NULL);
}


void net_client_base::OnClose()
{
	//
	close();
	//
	if (_disconn_cb)
		(*_disconn_cb)(_errno, this);
}

bool net_client_base::OnMessage(void* data, unsigned int len)
{
	if (_msg_cb)
		return (*_msg_cb)(this, data, len);
	return false;
}

	
void net_client_base::get_sock_name()
{
	struct sockaddr_in laddr;
	socklen_t slen = sizeof(laddr);
	getsockname(_fd, (struct sockaddr*)&laddr, &slen);
	
	_ip = ntohl(laddr.sin_addr.s_addr);
	_port = ntohs(laddr.sin_port);
}
	
void net_client_base::get_peer_name()
{
	struct sockaddr_in paddr;
	socklen_t slen = sizeof(paddr);
	getpeername(_fd, (struct sockaddr*)&paddr, &slen);
	_ip = ntohl(paddr.sin_addr.s_addr);
	_port = ntohs(paddr.sin_port);
}
	
char* net_client_base::get_ip(char*  ip)
{
	struct in_addr in;
	in.s_addr = htonl(_ip);
	return inet_ntoa(in);
}

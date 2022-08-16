#ifndef _NET_CLINET_BASE_H_
#define _NET_CLINET_BASE_H_

#if _WIN32 
#include <WinSock2.h>
#include <windows.h>
#else 
#include <inet/apra.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif 

typedef  void(*PCALLBACKFUNC)();

typedef  bool(*PMSGFUNC)(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nMsgLen);

class eventloop;

class net_client_base
{
public:
	net_client_base();
	virtual ~net_client_base();

public:
	virtual  void   OnRead() {}

	virtual  void	OnSend() {}

	virtual  unsigned int  get_wait_send_cnt() { return 0; }

	virtual int 	send_msg(const char* pData, unsigned int nMsgLen);
public:
#if _WIN32 
	SOCKET  create(int domain = AF_INET, int socket_type = SOCK_STREAM, int protocol_type = IPPROTO_IP);
#else 
	int		create(int domain = AF_INET, int socket_type = SOCK_STREAM, int protocol_type = IPPROTO_IP);
#endif
	//
	bool	connect(unsigned int host_ip, unsigned short port);
	//
	bool	bind(unsigned int host_ip, unsigned short port);
	//
	bool	listen(int backlog = 10);

	bool	set_tcp_nodelay();

	bool	set_nio(int mode = 1);

	bool	set_reuse_addr(bool flag = 1);

	bool	set_reuse_port(bool flag = 1);

	void	close();
public:
#if _WIN32 
	SOCKET		_fd;
#else 
	int			_fd;
#endif 
	time_t		_break_timestamp;
};


#endif


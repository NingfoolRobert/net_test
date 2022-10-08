#ifndef _NET_CLINET_BASE_H_
#define _NET_CLINET_BASE_H_

#include <stdio.h> 
#include <stdlib.h>
#ifdef _WIN32 
#include <WinSock2.h>
#include <windows.h>
#else 
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <error.h>
#endif 

class net_client_base;
class eventloop;

typedef  unsigned int(*PMSGLENPARSEFUNC)(void*, unsigned int);

typedef  void(*PDISCONNCALLBACK)(void*, net_client_base*);

typedef  bool(*PNETMSGCALLBACK)(void*, void* , unsigned int);

class eventloop;

class net_client_base
{
public:
	net_client_base(eventloop* loop, PNETMSGCALLBACK fnc, PDISCONNCALLBACK dis_conn_cb);
	virtual ~net_client_base();

public:
	virtual  void   OnRead() {}

	virtual  void	OnSend() {}

	virtual  unsigned int  get_wait_send_cnt() { return 0; }

	virtual int 	send_msg(const char* pData, unsigned int nMsgLen);

	virtual  void   OnTerminate();
public:
	bool OnMessage(void* data, unsigned int len);
public:
#ifdef _WIN32 
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
	
	void	terminate();
public:
#ifdef _WIN32 
	SOCKET		_fd;
#else 
	int			_fd;
#endif 
	time_t		_break_timestamp;
protected:
	eventloop*					_loop;
	PNETMSGCALLBACK				_msg_cb;
	PDISCONNCALLBACK			_dis_conn_cb;
	//
	unsigned int				_ip;
	unsigned short				_port;
};


#endif


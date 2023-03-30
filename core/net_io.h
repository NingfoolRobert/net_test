#ifndef _NET_IO_H_
#define _NET_IO_H_

#include <stdio.h> 
#include <stdlib.h>
#include <atomic>
#ifdef _WIN32 
#define WIN32_LEAN_AND_MEAN
#define  FD_SETSIZE		1024
#include <WinSock2.h>
#include <windows.h>
#define  ngx_sock		SOCKET 
#else 
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <error.h>
#define  ngx_sock		int
#endif 

#define EV_NULL			0x0000
#define EV_WRITE		0x0010
#define EV_READ			0x0001
#define EV_DELETED		0x1000

class eventloop;
class net_io;
//
typedef  size_t(*PMSGLENPARSEFUNC)(void*, size_t);
//connection break;
typedef  void(*PDISCONNCALLBACK)(int, net_io*);
//message arrive
typedef  bool(*PNETMSGCALLBACK)(net_io*, void* , unsigned int);

class net_io
{
public:
	net_io(PNETMSGCALLBACK fnc, PDISCONNCALLBACK dis_conn_cb);
	virtual ~net_io();

public:
	virtual  void   OnRecv() {}

	virtual  void	OnSend() {}

	virtual int 	send_msg(const char* pData, unsigned int nMsgLen) { return 0; }
public:
	ngx_sock	create(int domain = AF_INET, int socket_type = SOCK_STREAM, int protocol_type = IPPROTO_IP);
	//
	bool		connect(unsigned int host_ip, unsigned short port);
	//
	bool		bind(unsigned int host_ip, unsigned short port);
	//
	bool		listen(int backlog = 10);
	//
	int			send(const char* data, unsigned int len);
	//
	int			recv(char* data, unsigned int len);

	void		close();

	void		terminate();
public:
	bool		set_nio(int mode = 1);

	bool		set_reuse_addr(bool flag = 1);

	bool		set_reuse_port(bool flag = 1);

	void		get_sock_name();
	
	void		get_peer_name();

	bool		OnMessage(void* data, unsigned int len);

	void		OnClose();

	bool		set_sock_opt(int level, int optname, void* optval, int optlen);

	bool		get_sock_opt(int level, int optname, void* optval, int* optlen);
public:
	void		add_ref(){ ++_ref; }
	void		release(){if(--_ref == 0) delete this;}

protected:
	void		update_event(int ev);
public:
	ngx_sock					_fd;
	//
	time_t						_brk_tm;	//break timestamp
	int16_t						_errno;
	uint16_t					_ev;
	eventloop*					_loop;
	//
	unsigned int				_ip;		//host ip 
	unsigned int 				_port;		//
protected:
	PNETMSGCALLBACK				_msg_cb;
	PDISCONNCALLBACK			_disconn_cb;
private:
	std::atomic_int				_ref;
};


#endif //_NET_IO_H_


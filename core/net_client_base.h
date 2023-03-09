#ifndef _NET_CLINET_BASE_H_
#define _NET_CLINET_BASE_H_

#include <stdio.h> 
#include <stdlib.h>
#include <atomic>
#ifdef _WIN32 
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

class eventloop;
class net_client_base;
//
typedef  size_t(*PMSGLENPARSEFUNC)(void*, size_t);
//connection break;
typedef  void(*PDISCONNCALLBACK)(int, net_client_base*);
//message arrive
typedef  bool(*PNETMSGCALLBACK)(net_client_base*, void* , unsigned int);

class net_client_base
{
public:
	net_client_base(PNETMSGCALLBACK fnc, PDISCONNCALLBACK dis_conn_cb);
	virtual ~net_client_base();

public:
	virtual  void   OnRead() {}

	virtual  void	OnSend() {}

	virtual  size_t wait_sndmsg_size() { return 0; }

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

	bool		set_tcp_linger();

	bool		set_tcp_nodelay();

	bool		set_nio(int mode = 1);

	bool		set_reuse_addr(bool flag = 1);

	bool		set_reuse_port(bool flag = 1);

	void		get_sock_name();
	
	void		get_peer_name();
	
	char*		get_ip(char*  ip);
	
	void		close();
	
	void		terminate();

	bool		OnMessage(void* data, unsigned int len);

	void		OnClose();
public:
	void		add_ref(){ ++_ref; }
	void		release(){if(--_ref == 0) delete this;}
public:
	ngx_sock					_fd;
	//
	time_t						_brk_tm;	//break timestamp
	int							_errno;
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


#endif


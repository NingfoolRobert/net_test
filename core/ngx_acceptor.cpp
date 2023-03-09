#include "ngx_acceptor.h"
#include <cstdio>
#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else 
#include <netinet/in.h>
#include <sys/socket.h>
#endif 

ngx_acceptor::ngx_acceptor():net_io(NULL, NULL)
{
	_cb = NULL;
}

ngx_acceptor::~ngx_acceptor()
{

}

void ngx_acceptor::OnRead()
{
	struct sockaddr_in  caddr; 
	socklen_t slen = 0; 

#ifdef _WIN32 
	SOCKET cli = ::accept(_fd, (struct sockaddr*)&caddr, &slen);
#else 
	int cli = ::accept(_fd, (struct sockaddr*)&caddr, &slen);	
#endif 
	if(cli < 0)
	{
		printf("accept  client fail. ");
		return ;
	}
	// 
	if(_cb)
		_cb(cli);	
}
		
bool ngx_acceptor::init(unsigned int host_ip, unsigned short port, PACCEPTCALLBACK cb)
{
	_ip = host_ip;
	_port = port;
	_cb = cb;
	_fd = create();
	if(_fd < 0)
	{
		printf("create socket fail.\n");
		return false;
	}
	//
	if (!net_io::bind(host_ip, port))
	{
		printf("bind fail, port:%d\n", port);
		return false;
	}
	//
	if (!net_io::listen())
	{
		printf("listen fail. port:%d\n", port);
		return false;
	}
	//
	return true;
}

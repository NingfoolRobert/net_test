#include "ngx_acceptor.h"
#include <cstdio>
#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else 
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
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
	socklen_t slen = sizeof(caddr);
	ngx_sock cli = ::accept(_fd, (struct sockaddr*)&caddr, &slen);
#ifdef _WIN32 
	if(cli == INVALID_SOCKET){
		_errno = GetLastError();
#else 
	if(cli == -1) {
		_errno = errno;
#endif 
		printf("accept  client fail. err:%d ", _errno);
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
#ifdef _WIN32 
	if(_fd == INVALID_SOCKET) {
#else 
	if(_fd < 0){
#endif 
	
		printf("create socket fail, err:%d.\n", _errno);
		return false;
	}
	//
	if (!net_io::bind(host_ip, port))
	{
		printf("bind fail, err:%d, port:%d.\n", _errno, port);
		return false;
	}
	//
	if (!net_io::listen())
	{
		printf("listen fail, err:%d, port:%d.\n", _errno, port);
		return false;
	}
	//
	return true;
}

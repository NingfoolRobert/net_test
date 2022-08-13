#ifndef _NET_CLINET_BASE_H_
#define _NET_CLINET_BASE_H_

#if _WIN32 
#include <Windows.h>
#include <WinSock2.h>
#else 
#include <inet/apra.h>
#endif 

class net_client_base
{
public:
	net_client_base();
	virtual ~net_client_base();

public:
	virtual  void   OnRead();

	virtual  void	OnSend();

	virtual  unsigned int  GetWaitSendCnt() { return 0; }
public:
#if _WIN32 
	SOCKET		m_fd;
#else 
	int			m_fd;
#endif 
};


#endif


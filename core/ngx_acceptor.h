/**
 * @file	acceptor.h
 * @brief 
 * @author	ningbf
 * @version 1.0.0
 * @date	2023-02-28
 */
#ifndef _ACCEPTOR_H_
#define _ACCEPTOR_H_ 

#include "net_io.h"

typedef  void(*PACCEPTCALLBACK)(ngx_sock, struct sockaddr_in&);


class ngx_acceptor:
	public net_io
{
	public:
		ngx_acceptor();
		virtual ~ngx_acceptor();
	public:	
		virtual void  OnRecv();
	public:
		bool  init(unsigned int host_ip, unsigned short port, PACCEPTCALLBACK cb);
		
	private:
		PACCEPTCALLBACK			_cb;
};



#endif 


#ifndef _TCP_CONN_H_
#define _TCP_CONN_H_

#include "net_client_base.h"
#include <vector>

#include "memory_pool.h"

typedef  void(*PCALLBACKFUNC)();

typedef  bool(*PMSGFUNC)(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nMsgLen);

class eventloop;

typedef  unsigned int (*MSGLENPARSEFUNC)(const char* pData, int nDataLen);

class tcp_conn :
	public net_client_base
{
public:
	tcp_conn(CBizUser* pUser,  eventloop* eloop, MSGLENPARSEFUNC pfnc, unsigned int nHeadLen);
	virtual ~tcp_conn();
public:
	virtual void   OnRead();

	virtual void   OnSend();
	
public:
	bool   OnMessage(char* pData, unsigned int nDataLen);

	void   OnDisConnect();

	void   OnConnect();
public:
	bool   SendMsg(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nDataLen);
private:
	eventloop*			_loop;
	MSGLENPARSEFUNC		_pfnc;
	unsigned int		_head_len;

	PCALLBACKFUNC		_dis_conn_fnc;
	PMSGFUNC			_msg_fnc;


private:
	unsigned int		_expected_len;
	unsigned int		_recv_len;
	void*				_recv_buf;
	ngx_pool_t			*_pool;
};






#endif


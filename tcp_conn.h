#ifndef _TCP_CONN_H_
#define _TCP_CONN_H_

#include "net_client_base.h"
#include <vector>

#include "memory_pool.h"



class eventloop;

typedef  unsigned int (*MSGLENPARSEFUNC)(const char* pData, int nDataLen);

class tcp_conn :
	public net_client_base
{
public:
	tcp_conn(eventloop* eloop, MSGLENPARSEFUNC pfnc, unsigned int nHeadLen, PCALLBACKFUNC  dis_conn_fnc, PMSGFUNC msg_fnc);
	virtual ~tcp_conn();
public:
	virtual void   OnRead();

	virtual void   OnSend();
public:

public:
	bool   OnMessage(char* pData, unsigned int nDataLen);

	void   OnDisConnect();

	void   OnConnect();
public:
	bool   SendMsg(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nDataLen);
private:
	eventloop*			_loop;
	MSGLENPARSEFUNC		_msg_head_fnc;
	unsigned int		_head_len;

	PCALLBACKFUNC		_dis_conn_fnc;
	PMSGFUNC			_msg_fnc;


private:
	ngx_pool_t			*_pool;
	unsigned int		_expected_len;
	unsigned int		_recv_len;
	void*				_recv_buf;
	
	unsigned int		_send_len;
	unsigned int		_snded_len;
	void*				_send_buf;
};






#endif


#ifndef _TCP_CONN_H_
#define _TCP_CONN_H_

#include "net_client_base.h"


#include "memory_pool.h"
#include "cycle_memory_block.h"

#include <vector>
#include <mutex>

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

	virtual int	   send_msg(const char* pData, unsigned int nMsgLen);

	virtual  unsigned int  get_wait_send_cnt();
public:

public:
	bool   OnMessage(char* pData, unsigned int nDataLen);

	void   OnDisConnect();
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
	
	std::mutex			_lck;
	cycle_memory_block*	_snd_wait_buf;
};






#endif


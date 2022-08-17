#ifndef _TCP_CONN_H_
#define _TCP_CONN_H_

#include "net_client_base.h"


#include "memory_pool.h"
#include "cycle_memory_block.h"

#include <vector>
#include <mutex>

class eventloop;



class tcp_conn :
	public net_client_base
{
public:
	tcp_conn(eventloop* eloop, PMSGLENPARSEFUNC pfnc, unsigned int nHeadLen, PDISCONNCALLBACK  dis_conn_fnc, PNETMSGCALLBACK msg_fnc);
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
	PMSGLENPARSEFUNC		_msg_head_fnc;
	unsigned int		_head_len;

	PDISCONNCALLBACK		_dis_conn_fnc;
	PNETMSGCALLBACK			_msg_fnc;


private:
	ngx_pool_t			*_pool;
	unsigned int		_expected_len;
	unsigned int		_recv_len;
	void*				_recv_buf;
	
	std::mutex			_lck;
	cycle_memory_block*	_snd_wait_buf;
};






#endif


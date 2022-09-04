#ifndef _TCP_CONN_H_
#define _TCP_CONN_H_

#include "net_client_base.h"


#include "ngx_pool.h"
#include "cycle_memory_block.h"
#include "ngx_queue.h"
#include "ngx_buf.h"
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

	virtual void   OnTerminate();
private:
	PMSGLENPARSEFUNC		_msg_head_fnc;
	unsigned int			_head_len;
private:
	ngx_pool_t			*_pool;
	unsigned int		_expected_len;
	ngx_buf_t*			_rcv_buf;
	
	std::mutex			_lck;
	unsigned int		_snd_len;
	ngx_buf_t*			_snd_buf;
	ngx_queue_t*		_wait_snds;
};






#endif


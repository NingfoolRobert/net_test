#ifndef _TCP_CONN_H_
#define _TCP_CONN_H_

#include "net_io.h"
#include "ngx_pool.h"
#include "ngx_queue.h"
#include "ngx_buf.h"
#include <mutex>


class tcp_conn :
	public net_io
{
public:
	tcp_conn(unsigned int nHeadLen, PMSGLENPARSEFUNC pfnc, PNETMSGCALLBACK msg_fnc, PDISCONNCALLBACK disconn_cb);
	virtual ~tcp_conn();
public:
	virtual void   OnRecv();

	virtual void   OnSend();
	
	virtual int	   send_msg(const char* pData, unsigned int nMsgLen);

	virtual size_t  wait_sndmsg_size();
private:
	PMSGLENPARSEFUNC		_msg_head_fnc;
	size_t					_head_len;
private:
	ngx_pool_t			*_pool;
	size_t				_expected_len;
	ngx_buf_t*			_rcv_buf;
	
	std::mutex			_lck;
	size_t				_snd_len;
	ngx_buf_t*			_snd_buf;
	ngx_queue_t*		_wait_snds;
};






#endif


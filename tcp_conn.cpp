#include "tcp_conn.h"
#include "eventloop.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#include <winerror.h>
#include <errno.h>
#else 
#include <errno.h>
#endif 

tcp_conn::tcp_conn(eventloop* eloop, PMSGLENPARSEFUNC msg_head_fnc, unsigned int nHeadLen, PDISCONNCALLBACK dis_conn_fnc, PNETMSGCALLBACK pfnc) :
	net_client_base(eloop, pfnc, dis_conn_fnc),
	_msg_head_fnc(msg_head_fnc),
	_head_len(nHeadLen), 
	_expected_len(nHeadLen), 
	_recv_len(0), 
	_recv_buf(nullptr),
	_snd_wait_buf(nullptr)
{
	_pool = ngx_create_pool(1);
	_snd_wait_buf = new cycle_memory_block(2 * 1024 * 1024);
}

tcp_conn::~tcp_conn()
{
}

void tcp_conn::OnRead()
{
	if (_recv_buf == nullptr)
	{
		_recv_buf = ngx_palloc(_pool, _expected_len);
		if (nullptr == _recv_buf)
			return;
	}
	//
	int recv_len = ::recv(_fd, (char*)_recv_buf + _recv_len, _expected_len - _recv_len, 0);
	if (recv_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK)
#else 
		if (errno != EAGAIN || errno != EINTR)	
#endif 
			OnTerminate();
		return;
	}
	else if (recv_len == 0)//peer close 
	{
		OnTerminate();
		return;
	}

	//
	_recv_len += recv_len;
	if (_expected_len == _recv_len)
	{
		_expected_len = _msg_head_fnc((char*)_recv_buf, _recv_len);
		if (_expected_len > _head_len)
		{
			void* buf = ngx_palloc(_pool, _expected_len);
			if (NULL == buf)
				return;
			memcpy(buf, _recv_buf, _recv_len);
			ngx_free(_pool, _recv_buf);
			_recv_buf = buf;
		}
	}
	//
	if (_recv_len = _expected_len && _recv_len >= _head_len)
	{
		OnMessage((char*)_recv_buf,  _expected_len);
		_expected_len = _head_len;
		_recv_len = 0;
		return;
	}
}

void tcp_conn::OnSend()
{
	std::unique_lock<std::mutex> _(_lck);
	int send_len = net_client_base::send_msg(_snd_wait_buf->data(), _snd_wait_buf->c_length());
	if (send_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK)
#else 
		if (errno != EAGAIN && errno != EINTR)
#endif 
		{
			OnTerminate();
			return;
		}
	}
	else if (send_len == 0)
	{
		OnTerminate();
		return;
	}
	//
	_snd_wait_buf->pop(send_len);
}


int tcp_conn::send_msg(const char* pData, unsigned int nMsgLen)
{
	std::unique_lock<std::mutex> _(_lck);
	if (_snd_wait_buf == nullptr || _snd_wait_buf->size())
	{
		int send_len = net_client_base::send_msg(pData, nMsgLen);
		if (send_len < 0)
		{
#ifdef _WIN32
			if (GetLastError() != EWOULDBLOCK)
#else 
			if (errno != EAGAIN && errno != EINTR)
#endif 
			{
				OnTerminate();
				return -1;
			}
			//
			if (!_snd_wait_buf->append((void*)pData, nMsgLen))
			{
				return 1;
			}
			return 0;
		}
		else if (send_len == 0)
		{
			OnTerminate();
			return -1;
		}
		//
		if (send_len < nMsgLen)
		{
			if (!_snd_wait_buf->append((void*)(pData + send_len), nMsgLen - send_len))
			{
				return 1;
			}
		}
		else if (send_len == nMsgLen)
		{
			return 0;
		}
		return 1;
	}
	//
	if (!_snd_wait_buf->append((void*)pData, nMsgLen))
		return 1;
	return 0;
}

unsigned int tcp_conn::get_wait_send_cnt()
{
	std::unique_lock<std::mutex> _(_lck);
	return _snd_wait_buf->size();
}

void tcp_conn::OnTerminate()
{
	net_client_base::OnTerminate();
	net_client_base::close();
}

bool tcp_conn::OnMessage(char* pData, unsigned int nDataLen)
{
	if (_msg_cb)
		return _msg_cb(pData, nDataLen);
	return true;
}



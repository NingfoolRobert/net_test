#include "tcp_conn.h"
#include "ngx_core.h"

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
	_rcv_buf(NULL),
	_snd_buf(NULL)
{
	_pool = ngx_create_pool(1);
	_wait_snds = ngx_create_queue(_pool, sizeof(ngx_buf_t*), 1024);
}

tcp_conn::~tcp_conn()
{
}

void tcp_conn::OnRead()
{
	if (_rcv_buf == nullptr)
	{
		_rcv_buf = ngx_create_buf(_pool, _expected_len);
		if (nullptr == _rcv_buf)
			return;
	}
	//
	int recv_len = ::recv(_fd, (char*)_rcv_buf + _rcv_buf->len, _expected_len - _rcv_buf->len, 0);
	if (recv_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK) {
			ngx_log_error(((ngx_core_t*)(_loop->_core))->log, "net error(%d), ip:port=%d:%d...", GetLastError(), _ip, _port);
#else 
		if (errno != EAGAIN || errno != EINTR) {
			ngx_log_error(_loop->_log, "net error(%d), ip:port=%d:%d...", errno(), _ip, _port);
#endif 
			terminate();
		}
		return;
	}
	else if (recv_len == 0)//peer close 
	{
		ngx_log_warn(((ngx_core_t*)(_loop->_core))->log, "peer disconnect, ip:port=%d:%d...", _ip, _port);
		terminate();
		return;
	}

	//
	_rcv_buf->len += recv_len;
	if (_expected_len == _head_len)
	{
		_expected_len = _msg_head_fnc((char*)_rcv_buf->data, _rcv_buf->len);
		if (_expected_len > _head_len && _expected_len > _rcv_buf->cap)
		{
			ngx_buf_t* buf = ngx_create_buf(_pool, _expected_len);
			if (NULL == buf)
				return;
			memcpy(buf->data, _rcv_buf->data, _rcv_buf->len);
			ngx_free(_pool, _rcv_buf);
			_rcv_buf = buf;
		}
	}
	//
	if (_rcv_buf->len = _expected_len && _rcv_buf->len >= _head_len)
	{
		OnMessage((char*)_rcv_buf->data,  _expected_len);
		_expected_len = _head_len;
		_rcv_buf->len = 0;
		return;
	}
}

void tcp_conn::OnSend()
{
	std::unique_lock<std::mutex> _(_lck);
	if (NULL == _snd_buf )
	{
		if (ngx_queue_empty(_wait_snds))
			return;
		_snd_buf = (ngx_buf_t*)ngx_queue_get(_wait_snds);
	}
	//
	if (NULL == _snd_buf || _snd_buf->len <= _snd_len)
		return;
	//
	int send_len = net_client_base::send_msg((char*)(_snd_buf->data) + _snd_len, _snd_buf->len - _snd_len);
	if (send_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK){
			ngx_log_warn(((ngx_core_t*)(_loop->_core))->log, "send error(%d), ip:port=%d:%d...", GetLastError(), _ip, _port);
#else 
		if (errno != EAGAIN && errno != EINTR) {
			ngx_log_warn(_loop->_core->log, "send error(%d), ip:port=%d:%d...", errno, _ip, _port);
#endif 
			terminate();
			return;
		}
	}
	else if (send_len == 0)
	{
		ngx_log_warn(((ngx_core_t*)(_loop->_core))->log, "peer disconnect, ip:port=%d:%d...", _ip, _port);
		terminate();
		return;
	}
	//
	_snd_len += send_len;
	if (_snd_len == _snd_buf->len)
	{
		ngx_free(_pool, _snd_buf);
		_snd_buf = NULL;
		_snd_len = 0;
	}
}

int tcp_conn::send_msg(const char* pData, unsigned int nMsgLen)
{
	std::unique_lock<std::mutex> _(_lck);
	if (_snd_buf || !ngx_queue_empty(_wait_snds))
	{
		ngx_buf_t* buf = ngx_create_buf(_pool, nMsgLen);
		if (NULL == buf)
		{
			return -1;
		}
		memcpy(buf->data, pData, nMsgLen);
		buf->len = nMsgLen;
		if (ngx_queue_push(_wait_snds, &buf))
			return 0;
		return -1;
	}
	//
	int snd_len = net_client_base::send_msg(pData, nMsgLen);
	if (snd_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK)
		{
			ngx_log_warn(((ngx_core_t*)(_loop->_core))->log, "net error(%d), ip:port=%d:%d...", GetLastError(), _ip, _port);
#else 
		if (errno != EAGAIN && errno != EINTR)
		{
			ngx_log_warn(((ngx_core_t*)(_loop->_core))->_log, "net error(%d), ip:port=%d:%d...", errno, _ip, _port);
#endif 
			terminate();
			return -1;
		}
		
		ngx_buf_t* buf = ngx_create_buf(_pool, nMsgLen);
		if (NULL == buf)
		{
			return -1;
		}

		memcpy(buf->data, pData, nMsgLen);
		buf->len = nMsgLen;
		if (ngx_queue_push(_wait_snds, &buf))
			return 0;
		return -1;
	}
	else if (snd_len == 0)
	{
		ngx_log_warn(((ngx_core_t*)(_loop->_core))->log, "peer disconnect...");
		terminate();
		return -1;
	}
	//
	if (nMsgLen == snd_len)
		return 0;
	//
	_snd_len = snd_len;
	_snd_buf = ngx_create_buf(_pool, nMsgLen);
	memcpy(_snd_buf->data, pData, nMsgLen);
	_snd_buf->len = nMsgLen;
	return 0;
}

unsigned int tcp_conn::get_wait_send_cnt()
{
	std::unique_lock<std::mutex> _(_lck);
	return ngx_queue_size(_wait_snds);
}

void tcp_conn::OnTerminate()
{
	net_client_base::OnTerminate();
	if(_rcv_buf) _rcv_buf->len = 0;
	_snd_len = 0;
}



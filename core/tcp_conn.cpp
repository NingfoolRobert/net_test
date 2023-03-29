#include "tcp_conn.h"
#include <string.h>
#include <errno.h>
#include <mutex>
//
#ifdef _WIN32
#include <ws2tcpip.h>
#include <winerror.h>
#endif 

tcp_conn::tcp_conn(unsigned int nHeadLen, PMSGLENPARSEFUNC msg_head_fnc, PNETMSGCALLBACK pfnc, PDISCONNCALLBACK disconn_cb) :
	net_io(pfnc, disconn_cb),
	_msg_head_fnc(msg_head_fnc),
	_head_len(nHeadLen), 
	_expected_len(nHeadLen), 
	_rcv_buf(NULL),
	_snd_len(0),
	_snd_buf(NULL)
{
	_pool = ngx_create_pool(1);
	_wait_snds = ngx_create_queue(_pool, sizeof(ngx_buf_t*), 1024);
}

tcp_conn::~tcp_conn()
{
	ngx_free(_pool, _rcv_buf);
	_rcv_buf = NULL;
	ngx_free(_pool, _snd_buf);
	_snd_buf = NULL;
	ngx_free(_pool, _wait_snds);
}

void tcp_conn::OnRecv()
{
	if (_rcv_buf == nullptr)
	{
		_rcv_buf = ngx_create_buf(_pool, _expected_len);
		if (nullptr == _rcv_buf)
			return;
	}
	//
	int recv_len = net_io::recv((char*)_rcv_buf->data + _rcv_buf->len, (unsigned int)(_expected_len - _rcv_buf->len));
	if (recv_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK) {
			_errno = GetLastError();
#else 
		if (errno != EAGAIN && errno != EINTR) {
			_errno = errno;
#endif 
			terminate();
		}
		return;
	}
	else if (recv_len == 0)//peer close 
	{
		_errno = -1;
		terminate();
		return;
	}

	//
	_rcv_buf->len += (unsigned int)recv_len;
	if (_expected_len == _head_len)
	{
		_expected_len += _msg_head_fnc((char*)_rcv_buf->data, _rcv_buf->len);
		if (_expected_len > _rcv_buf->cap)
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
	if (_rcv_buf->len >= _expected_len)
	{
		OnMessage((char*)_rcv_buf->data,  (unsigned int)_expected_len);
		_expected_len = _head_len;
		_rcv_buf->len = 0;
		return;
	}
}


void tcp_conn::OnSend()
{
	std::unique_lock<spinlock> _(_lck);
	if (NULL == _snd_buf )
	{
		if (ngx_queue_empty(_wait_snds)) {
			update_event(EV_READ);
			return;
		}
		_snd_buf = (ngx_buf_t*)ngx_queue_get(_wait_snds);
	}
	//
	if (NULL == _snd_buf || _snd_buf->len <= _snd_len)
		return;
	//
	int send_len = net_io::send((char*)(_snd_buf->data) + _snd_len, (unsigned int)(_snd_buf->len - _snd_len));
	if (send_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK){
			_errno = GetLastError();
#else 
		if (errno != EAGAIN && errno != EINTR) {
			_errno = errno;
#endif 
			terminate();
			return;
		}
	}
	else if (send_len == 0)
	{
		_errno = -1;
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
	if (_brk_tm)
		return -1;
	//
	std::unique_lock<spinlock> _(_lck);
	if (_snd_buf || !ngx_queue_empty(_wait_snds))
	{
		ngx_buf_t* buf = ngx_create_buf(_pool, nMsgLen);
		if (NULL == buf)
		{
			return -1;
		}
		memcpy(buf->data, pData, nMsgLen);
		buf->len = nMsgLen;
		if (ngx_queue_push(_wait_snds, &buf)) {
			update_event(EV_READ | EV_WRITE);
			return 0;
		}
		return -1;
	}
	//
	int snd_len = net_io::send(pData, nMsgLen);
	if (snd_len < 0)
	{
#ifdef _WIN32
		if (GetLastError() != EWOULDBLOCK){
			_errno = GetLastError();
#else 
		if (errno != EAGAIN && errno != EINTR){
			_errno = errno;
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
		if (ngx_queue_push(_wait_snds, &buf)) {
			update_event(EV_READ | EV_WRITE);
			return 0;
		}
		return -1;
	}
	else if (snd_len == 0)
	{
		_errno = -1;
		terminate();
		return -1;
	}
	//
	if (nMsgLen == (unsigned int)snd_len)
		return 0;
	//
	_snd_len = snd_len;
	_snd_buf = ngx_create_buf(_pool, nMsgLen);
	memcpy(_snd_buf->data, pData, nMsgLen);
	_snd_buf->len = nMsgLen;
	update_event(EV_READ | EV_WRITE);
	return 0;
}

size_t tcp_conn::wait_sndmsg_size()
{
	std::unique_lock<spinlock> _(_lck);
	return ngx_queue_size(_wait_snds);
}




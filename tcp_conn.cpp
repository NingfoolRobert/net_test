#include "tcp_conn.h"
#include "eventloop.h"

tcp_conn::tcp_conn(eventloop* eloop, MSGLENPARSEFUNC msg_head_fnc, unsigned int nHeadLen, PCALLBACKFUNC dis_conn_fnc, PMSGFUNC pfnc) :
	_loop(eloop),
	_msg_head_fnc(msg_head_fnc),
	_msg_fnc(pfnc),
	_dis_conn_fnc(dis_conn_fnc),
	_head_len(nHeadLen), 
	_expected_len(nHeadLen), 
	_recv_len(0), 
	_recv_buf(nullptr),
	_send_len(0),
	_snded_len(0),
	_send_buf(nullptr)

{
	_pool = ngx_create_pool(1);
}

tcp_conn::~tcp_conn()
{
}

void tcp_conn::OnRead()
{
	if (_recv_buf == nullptr)
	{
		_recv_buf = ngx_allocate(_pool, _expected_len);
		if (nullptr == _recv_buf)
			return;
	}
	//
	int recv_len = ::recv(m_fd, (char*)_recv_buf + _recv_len, _expected_len - _recv_len, 0);
	if (recv_len < 0)
	{
#if _WIN32
		if (GetLastError() != EWOULDBLOCK)
			OnDisConnect();
#else 
		if (errno != EAGAIN || errno != EINTR)
			OnDisConnect();
#endif 
		return;
	}
	else if (recv_len == 0)//peer close 
	{
		OnDisConnect();
		return;
	}

	//
	_recv_len += recv_len;
	if (_expected_len == _recv_len)
	{
		_expected_len = _msg_head_fnc((char*)_recv_buf, _recv_len);
		if (_expected_len > _head_len)
		{
			void* buf = ngx_allocate(_pool, _expected_len);
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

}


bool tcp_conn::OnMessage(char* pData, unsigned int nDataLen)
{
	unsigned int nMsgID;
	unsigned int nMsgNo;
	void*		 pData;
	unsigned int nMsgLen = 0;
	_msg_fnc(nMsgID, nMsgNo, pData, nMsgLen);
}

void tcp_conn::OnDisConnect()
{
	if (_loop)
		_loop->remove(this);
	//
	if(_dis_conn_fnc)
		_dis_conn_fnc();
}

void tcp_conn::OnConnect()
{

}


bool tcp_conn::SendMsg(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nDataLen)
{
	//TODO 
	return true;
}

#include "BizUser.h"
#include "eventloop.h"
#include "Impl.h"
#include "tcp_conn.h"
#include "ngx_log.h"


#include <string.h>
#include <thread>



//
class CBizUser* pUser = nullptr;
CBizUser::Impl* g_impl = nullptr;
///////////////////////////////////////
void  OnNetDisConn() {
	//print disconnet 
	pUser->OnDisConnect();
	//
	if(g_impl->cfg.auto_reconnect)
		pUser->reconnect();
}
//
bool OnMessage(const char* data, unsigned int len)
{
	if (nullptr == pUser)
		return true;
	//log on msg 
	if (*data == 1)
	{
		pUser->OnLogon(0);
	}
	//heartbeat
	if (len == 20)
	{
		pUser->OnHeartBeat();
	}
	//
	return pUser->OnMessage(0, 0, (void*)data, len);
}
//
bool OnNetMsg(char* pData, unsigned int nMsgLen)
{
	unsigned int nMsgID, nMsgNo;
	if (nMsgID == 1)
	{
		pUser->OnLogon(nMsgNo);
		if (nMsgNo == 0)	//log on success
		{
			//TODO ¶¨Ê±ÐÄÌø
			//g_impl->loop->add_timer();
		}
		return true;
	}
	//
	return pUser->OnMessage(nMsgID, nMsgNo, pData, nMsgLen);
}
//
void OnHeartBeatTimer(void* param)
{
	//heartbeat message to send
	//g_impl->conn->send_msg()
	if (g_impl->conn->_break_timestamp)
		return;
	//gene heartbeat message 
	g_impl->conn->send_msg(nullptr, 0);
}


//////////////////////////////////////
void  ActiveWorkThread(CBizUser::Impl* impl) {
	
	std::vector<net_client_base*> vecTmp;
	while (impl->started)
	{
		impl->loop->loop(vecTmp, 10);
	}
#ifdef _WIN32 
	WSACleanup();
#endif 
	//
	char szTmp[1024] = { 0 };
	ngx_log_info("%s api stoped ...", g_impl->api_name);

}



///////////////////////////////////////
CBizUser::CBizUser()
{
	_impl = new Impl;
	g_impl = _impl;
	_impl->logoned = 0;
	_impl->started = 0;
	pUser = this;
}


CBizUser::~CBizUser()
{
	if (_impl->conn)
	{
		delete _impl->conn;
		_impl->conn = NULL;
	}
	//
	if (_impl->loop)
	{
		delete _impl->loop;
		_impl->loop = NULL;
	}
	
	if (_impl)
	{
		delete _impl;
		_impl = NULL;
	}
}

bool CBizUser::start(const COMMONCFG& cfg)
{
	if (0 != _impl->cfg.url[0])// || _impl->started)
	{
		ngx_log_fatal("init %s fail, not define url", _impl->api_name);
		return false;
	}
	//
	if (_impl->started)
	{
		ngx_log_warn("init %s fail, api started", _impl->api_name);
		return false;
	}
#ifdef _WIN32 
	WSADATA  wsa_data;
	WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif 
	_impl->init();
	//
#if _WIN32 

	SOCKET sock = _impl->conn->create();
	if(INVALID_SOCKET == sock)
#else 
	int sock = _impl->conn->Create();
	if (sock == -1)
#endif 
	{
		return false;
	}
	_impl->conn->set_tcp_nodelay();
	if (!_impl->conn->connect(_impl->host_ip[_impl->ip_idx],  _impl->port))
	{
		//LogError("connect server fail. ip:port=%s:%d", _impl->cfg.ip, _impl->cfg.port);
		delete _impl->conn;
		delete _impl->loop;
		return false;
	}
	//
	OnConnect();
	//
	_impl->conn->set_nio();
	_impl->loop->add(_impl->conn);
	_impl->started = 1;

	std::thread* thr = new std::thread(ActiveWorkThread, _impl);
	thr->detach();
	//start log on 
	
	//
	return true;
}

void CBizUser::stop()
{
	_impl->started = false;	
	_impl->loop->wakeup();
}

void CBizUser::reconnect()
{
	if (_impl->conn->_break_timestamp == 0 || _impl->cfg.auto_reconnect) 
		return;
	//
	if (NULL == _impl->conn || NULL == _impl->loop)
		return;
	
	_impl->loop->remove(_impl->conn);

	if (!_impl->connect())
	{
		return;
	}
	//
	_impl->loop->add(_impl->conn);
	OnConnect();
	//TODO logon
	
	//
}

bool CBizUser::send_message(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nMsgLen)
{
	if (!_impl->started || !_impl->logoned || nMsgLen == 0 || nullptr == pData)
		return false;

	if (nullptr == _impl->conn || _impl->conn->_break_timestamp)
		return false;
	//
	return _impl->conn->send_msg(pData, nMsgLen);
}


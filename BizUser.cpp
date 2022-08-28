#include "BizUser.h"
#include "eventloop.h"
#include "Impl.h"
#include "tcp_conn.h"
#include "ngx_log.h"


#include <string.h>
#include <thread>

///////////////////////////////////////
void  OnNetDisConn(net_client_base* conn) {
// 	CBizUser::Impl *impl = (CBizUser::Impl*)param;
// 	CBizUser* pUser = (CBizUser*)(impl->biz_user);
/*	pUser->OnDisConnect();*/
}
//
bool OnMessage(const char* data, unsigned int len)
{
	void* param;
	CBizUser::Impl *impl = (CBizUser::Impl*)param;
	CBizUser* pUser = (CBizUser*)(impl->biz_user);
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
	return true;
// 	unsigned int nMsgID, nMsgNo;
// 	if (nMsgID == 1)
// 	{
// 		pUser->OnLogon(nMsgNo);
// 		if (nMsgNo == 0)	//log on success
// 		{
// 			//TODO ¶¨Ê±ÐÄÌø
// 			//g_impl->loop->add_timer();
// 		}
// 		//
// 		return true;
// 	}
// 	//
// 	return pUser->OnMessage(nMsgID, nMsgNo, pData, nMsgLen);
}
//
// void OnHeartBeatTimer(void* param)
// {
// 	//heartbeat message to send
// 	//g_impl->conn->send_msg()
// 	if (g_impl->conn->_break_timestamp)
// 		return;
// 	//todo gene heartbeat message 
// 	g_impl->conn->send_msg(nullptr, 0);
// }

// 
// void  ActiveWorkThread(CBizUser::Impl* impl) {
// 	
// 	std::vector<net_client_base*> vecTmp;
// 	while (impl->started)
// 	{
// 		impl->loop->loop(vecTmp, 10);
// 	}
// #ifdef _WIN32 
// 	WSACleanup();
// #endif 
// 	//
// 	ngx_log_info(impl->log, "%s api stoped ...", g_impl->api_name);
// 
// }

///////////////////////////////////////
CBizUser::CBizUser()
{
	_impl = new Impl;
	_impl->biz_user = this;
}


CBizUser::~CBizUser()
{
	if (_impl)
	{
		stop();
		delete _impl;
		_impl = NULL;
	}

}

bool CBizUser::start(const COMMONCFG& cfg)
{
	//
	if (!_impl->init()) {
		return false;
	}
	//
#if _WIN32 
	SOCKET sock = _impl->conn->create();
	if(INVALID_SOCKET == sock)
#else 
	int sock = _impl->conn->create();
	if (sock == -1)
#endif 
	{
		return false;
	}
	_impl->conn->set_tcp_nodelay();
	if (!_impl->connect())
	{
		return false;
	}
	//
	ngx_log_info(_impl->get_log(), "connect server success, ip:port=%s", _impl->core->cfg.url);
	OnConnect();
	//
	_impl->async(_impl->conn);
	//
	return _impl->logon();
}

void CBizUser::stop()
{
	if(_impl->conn && _impl->conn->_break_timestamp == 0)
		_impl->conn->OnTerminate();
	//
	_impl->uninit();
	ngx_log_info(_impl->get_log(), "stoped %s...", _impl->core->api_name);
}

bool  CBizUser::reconnect()
{
	if (_impl->conn->_break_timestamp == 0 || _impl->core->cfg.auto_reconnect)
		return false;
	//
	_impl->core->loop->remove(_impl->conn);
	if (!_impl->connect())
	{
		return false;
	}
	//
	OnConnect();
	//
	_impl->logon();
	_impl->async(_impl->conn);
}

bool CBizUser::send_message(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nMsgLen)
{
	if (!_impl->logoned || nMsgLen == 0 || nullptr == pData)
		return false;

	if (nullptr == _impl->conn || _impl->conn->_break_timestamp)
		return false;
	//
	return _impl->conn->send_msg(pData, nMsgLen);
}


#include "BizUser.h"
#include "eventloop.h"
#include "Impl.h"
#include "tcp_conn.h"
#include "ngx_log.h"
#include <string.h>

#define  NET_API_VERSION		0x01

/////////////////////////////////////////////////////////
unsigned int msg_head_parse(void* data, unsigned int nLen)
{
	return 0;
}
///////////////////////////////////////
void  OnNetDisConn(void* param, net_client_base* conn) {

	ngx_core_t* core = (ngx_core_t*)param;
 	CBizUser::Impl *impl = (CBizUser::Impl*)(core->biz);
 	CBizUser* pUser = (CBizUser*)(impl->biz);
	ngx_log_warn(core->log, "disconnect server, ip:port=%s:%d", core->host_ip[core->ip_idx], core->port[core->ip_idx]);
	pUser->OnDisConnect();
	if (core->cfg.auto_reconnect && core->started)
		pUser->reconnect();
	//
}
///////////////////////////////////////
bool OnNetMsg(void* param, void* data, unsigned int len)
{
	ngx_core_t* core = (ngx_core_t*)param;
	CBizUser::Impl *impl = (CBizUser::Impl*)(core->biz);
	CBizUser* pUser = (CBizUser*)(impl->biz);
	//log on msg 
	if (*(int*)data == 1)
	{
		int ret = 0;
		pUser->OnLogon(ret);
		if (0 == ret)//log on success; 
		{
			ngx_log_info(core->log, "log on success, userid:%d,...", core->cfg.userid);
			//heartbeat timer;
		}
		else//log on fail; 
		{
			ngx_log_error(core->log,"log on server fail. err:%d, reason:%s", 1, "not find the userid or password");
			impl->uninit();
		}
		return true;
	}
	//heartbeat
	if (len == 20)
	{
		pUser->OnHeartBeat();
		return true;
	}
	//
	return pUser->OnMessage(0, 0, (void*)data, len);
}

///////////////////////////////////////
CBizUser::CBizUser()
{
	_impl = new Impl(msg_head_parse, OnNetMsg, OnNetDisConn);
	_impl->core.biz = _impl;
	_impl->biz = this;
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
	memcpy(&_impl->core.cfg, &cfg, sizeof(cfg));
	//
	if (!_impl->init("net_test_api", NET_API_VERSION)) {
		return false;
	}
	//
	ngx_log_info(_impl->get_log(), "start connect server, ip:port=%s", _impl->core.cfg.url);
	if (!_impl->connect())
	{
		return false;
	}
	//
	ngx_log_info(_impl->get_log(), "connect server success, ip:port=%s", _impl->core.cfg.url);
	OnConnect();
	//
	_impl->async(_impl->conn);
	//
	return _impl->logon();
}

void CBizUser::stop()
{
	//
	_impl->uninit();
	ngx_log_info(_impl->get_log(), "stoped %s...", _impl->core.api_name);
}

bool  CBizUser::reconnect()
{
	if (_impl->conn->_break_timestamp == 0)
		return false;
	//
	ngx_log_info(_impl->get_log(), "start reconnect server...");
	//
	if (!_impl->connect())
	{
		return false;
	}
	//
	OnConnect();
	//
	_impl->logon();
	_impl->async(_impl->conn);
	return true;
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


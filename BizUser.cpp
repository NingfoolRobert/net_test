#include "BizUser.h"
#include "eventloop.h"
#include "Impl.h"
#include "tcp_conn.h"
#include "ngx_log.h"


#include <string.h>
#include <thread>

///////////////////////////////////////
void  OnNetDisConn(ngx_core_t* core, net_client_base* conn) {
 	CBizUser::Impl *impl = (CBizUser::Impl*)(core->biz);
 	CBizUser* pUser = (CBizUser*)(impl->biz);
	pUser->OnDisConnect();
	ngx_log_warn(core->log, "disconnect server, ip:port=%s:%d", core->host_ip[core->ip_idx], core->port[core->ip_idx]);
}
//
bool OnMessage(ngx_core_t* core, const char* data, unsigned int len)
{
	CBizUser::Impl *impl = (CBizUser::Impl*)(core->biz);
	CBizUser* pUser = (CBizUser*)(impl->biz);
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

///////////////////////////////////////
CBizUser::CBizUser()
{
	_impl = new Impl;
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
	if (!_impl->init()) {
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
	if(_impl->conn && _impl->conn->_break_timestamp == 0)
		_impl->conn->OnTerminate();
	//
	_impl->uninit();
	ngx_log_info(_impl->get_log(), "stoped %s...", _impl->core.api_name);
}

bool  CBizUser::reconnect()
{
	if (_impl->conn->_break_timestamp == 0 || _impl->core.cfg.auto_reconnect)
		return false;
	//
	_impl->core.loop->remove(_impl->conn);
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


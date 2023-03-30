#include "BizUser.h"
#include "eventloop.h"
#include "Impl.h"
#include "ngx_core.h"
#include "tcp_conn.h"
#include "ngx_log.h"
#include "version.h"
#include <string.h>

ngx_core_t*  g_core = NULL;
/////////////////////////////////////////////////////////
size_t msg_head_parse(void* data, size_t nLen)
{
	return 0;
}
///////////////////////////////////////
void  OnNetDisConn(int err, net_io* conn) {

	CBizUser* pUser = (CBizUser*)(g_core->biz);
	ngx_log_warn(g_core->log, "disconnect server,err:%d, ip:port=%s:%d", g_core->host_ip[g_core->ip_idx],err, g_core->port[g_core->ip_idx]);
	pUser->OnDisConnect();
	if (g_core->cfg.auto_reconnect && g_core->started)
		pUser->reconnect();
	//
}
///////////////////////////////////////
bool OnNetMsg(net_io* conn, void* data, unsigned int len)
{
	CBizUser* pUser = (CBizUser*)(g_core->biz);
	//log on msg 
	if (*(int*)data == 1)
	{
		int ret = 0;
		pUser->OnLogon(ret);
		if (0 == ret)//log on success; 
		{
			ngx_log_info(g_core->log, "log on success, userid:%d,...", g_core->cfg.userid);
			//heartbeat timer;
		}
		else//log on fail; 
		{
			ngx_log_error(g_core->log,"log on server fail. err:%d, reason:%s", 1, "not find the userid or password");
			pUser->stop();
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
	g_core = &_impl->core;	
	_impl->core.biz = _impl;
	_impl->biz = this;
}


CBizUser::~CBizUser()
{
	if (_impl)
	{
		if(_impl->core.started)
			stop();
		//
		delete _impl;
		_impl = NULL;
	}

}

bool CBizUser::start(const COMMONCFG& cfg)
{
	memcpy(&_impl->core.cfg, &cfg, sizeof(cfg));
	//
	if (!_impl->init("BIZ", 12)) {
		return false;
	}
	//
	ngx_log_info(_impl->get_log(), "%s %d.%d.%d, init start ...", _impl->core.api_name, 
			_impl->core.api_version & 0xFF0000 >> 16, 
			_impl->core.api_version & 0xFF00 >> 8, 
			_impl->core.api_version);
	//
	ngx_log_info(_impl->get_log(), "start connect server, ip:port=%s", _impl->core.cfg.url);
	if (!_impl->connect())
	{
		ngx_log_fatal(_impl->get_log(), "connect server fail, ip:port=%s", _impl->core.cfg.url);
		_impl->uninit();
		return false;
	}
	//
	ngx_log_info(_impl->get_log(), "connect server success, ip:port=%s", _impl->core.cfg.url);
	OnConnect();
	//
	_impl->async(_impl->conn);
	//
	if(!_impl->logon())
	{
		_impl->uninit();
		return false;
	}
	return true;
}

void CBizUser::stop()
{
	//
	_impl->uninit();
	ngx_log_info(_impl->get_log(), "stoped %s...", _impl->core.api_name);
}

bool  CBizUser::reconnect()
{
	if (_impl->conn->_brk_tm == 0)
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

bool CBizUser::send_message(unsigned int nMsgID, unsigned int nMsgNo, const char* pData, unsigned int nMsgLen)
{
	if (!_impl->logoned || nMsgLen == 0 || nullptr == pData)
		return false;

	if (nullptr == _impl->conn || _impl->conn->_brk_tm)
		return false;
	//
	return _impl->conn->send_msg(pData, nMsgLen);
}

	
const char*  CBizUser::get_version()
{
	return API_VERSION;
}

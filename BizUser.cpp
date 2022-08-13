#include "pch.h"
#include "BizUser.h"
#include "eventloop.h"


#include "tcp_conn.h"

struct CBizUser::Impl {
	COMMONCFG	cfg;
	eventloop	*loop;
	tcp_conn*	conn;
	short		logoned;
	short		started;
};
///////////////////////////////////////






CBizUser::CBizUser()
{
	_impl = new Impl;
	_impl->logoned = 0;
	_impl->started = 0;
}


CBizUser::~CBizUser()
{
}

bool CBizUser::start(COMMONCFG* cfg)
{
	if (nullptr == cfg)
		return false;
	//
	memcpy(&_impl->cfg, cfg, sizeof(COMMONCFG));
	//
	_impl->loop = new eventloop;
	_impl->conn = new tcp_conn(this, _impl->loop, nullptr, 16);
	if (nullptr == _impl->conn)
	{
		char szTmp[1024] = { 0 };
		sprintf(szTmp, "memory error!");
		OnLogData(5, szTmp);
		return false;
	}

	std::thread* thr = new std::thread(eventloop::loop, _impl->loop);
	_impl->started = 1;
	return true;
}

void CBizUser::stop()
{
	//TODO 
}

void CBizUser::reconnect()
{
	_impl->loop->remove(_impl->conn);
	tcp_conn* pConn = new tcp_conn(this, _impl->loop, nullptr, 16);
	//TODO conenct 
	
	//TODO logon
}

bool CBizUser::send_message(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nMsgLen)
{
	if (!_impl->started || !_impl->logoned || nMsgLen == 0 || nullptr == pData)
		return false;

	if (_impl->conn)
		return _impl->conn->SendMsg(nMsgID, nMsgNo, pData, nMsgLen);

	return false;
}


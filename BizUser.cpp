#include "BizUser.h"
#include "eventloop.h"

#include "tcp_conn.h"


class CBizUser* pUser = nullptr;


struct CBizUser::Impl {
	COMMONCFG		cfg;
	eventloop		*loop;
	tcp_conn*		conn;
	short			logoned;
	short			started;
	unsigned int	timeout;		//event loop ms;
	CBizUser*		user;
	///////////////////////////////
	Impl():loop(nullptr),conn(nullptr),logoned(0),started(0),timeout(10) {

		if (cfg.hearbeat_int == 0) cfg.hearbeat_int = 30;
		if (cfg.log_level == 0) cfg.log_level = 4;
	}
	~Impl() {
		if (loop)
		{
			delete loop;
			loop = nullptr;
		}
		if (conn)
		{
			delete conn;
			conn = nullptr;
		}
	}
};
///////////////////////////////////////

void  OnNetDisConn() {
	pUser->OnDisConnect();
}

bool OnNetMsg(unsigned int nMsgID, unsigned int nMsgNo, char* pData, unsigned int nMsgLen)
{
	if (nMsgID == 1)
	{
		pUser->OnLogon(nMsgNo);
		return true;
	}
	//
	return pUser->OnMessage(nMsgID, nMsgNo, pData, nMsgLen);
}

//////////////////////////////////////

void  ActiveWorkThread(CBizUser::Impl* impl) {
	
	std::vector<net_client_base*> vecTmp;
	while (impl->started)
	{
		impl->loop->loop(vecTmp, 10);
	}
	//TODO print exit
}



///////////////////////////////////////
CBizUser::CBizUser()
{
	_impl = new Impl;
	_impl->logoned = 0;
	_impl->started = 0;
	pUser = this;
}


CBizUser::~CBizUser()
{
}

bool CBizUser::start(COMMONCFG* cfg)
{
	if (nullptr == cfg || _impl->started)
		return false;
	//
	memcpy(&_impl->cfg, cfg, sizeof(COMMONCFG));
	//
	_impl->loop = new eventloop;
	_impl->conn = new tcp_conn(this, _impl->loop, nullptr, 16, OnNetDisConn, OnNetMsg);
	if (nullptr == _impl->conn)
	{
		char szTmp[1024] = { 0 };
		sprintf(szTmp, "memory error!");
		OnLogData(5, szTmp);
		return false;
	}
	//
	_impl->started = 1;
	std::thread* thr = new std::thread(ActiveWorkThread, _impl);
	thr->detach();
	//
	return true;
}

void CBizUser::stop()
{
	//TODO 
	_impl->started = false;	//TODO 
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


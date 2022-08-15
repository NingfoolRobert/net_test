#include "BizUser.h"
#include "eventloop.h"

#include "tcp_conn.h"


class CBizUser* pUser = nullptr;
CBizUser::Impl* g_impl = nullptr;

struct CBizUser::Impl {
	COMMONCFG				cfg;
	eventloop				*loop;
	net_client_base*		conn;
	unsigned int			timeout;		//event loop ms;
	unsigned char			logoned;
	unsigned char			started;
	unsigned char			ip_cnt;			
	unsigned char			ip_idx;
	unsigned int			host_ip[4];		//DNS => host_ip
	unsigned short			port;
	char					api_name[26];
	///////////////////////////////
	Impl():loop(nullptr),conn(nullptr),logoned(0),started(0),timeout(10),ip_cnt(0), ip_idx(0){

		if (cfg.hearbeat_int == 0) cfg.hearbeat_int = 30;
		if (cfg.log_level == 0) cfg.log_level = 4;
	}
	//
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
	if(*data == 1)
		pUser->OnLogon(0);
	//heartbeat
	if(len == 20)
		pUser->OnHeartBeat();
	//
	return pUser->OnMessage(0, 0, (void*)data, len);
}
//
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
//
void OnHeartBeatTimer()
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
	//
	char szTmp[1024] = { 0 };
	sprintf(szTmp, "%s api stoped ...", g_impl->api_name);
	pUser->OnLogData(4, szTmp);
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

bool CBizUser::start(COMMONCFG* cfg)
{
	if (nullptr == cfg || _impl->started)
		return false;
	//
	memcpy(&_impl->cfg, cfg, sizeof(COMMONCFG));
	//
	if(nullptr == _impl->loop)
		_impl->loop = new eventloop;
	//
	if (nullptr == _impl->conn)
	{
		_impl->conn = new tcp_conn(_impl->loop, nullptr, 16, OnNetDisConn, OnNetMsg);
		if (nullptr == _impl->conn)
		{
			char szTmp[1024] = { 0 };
			sprintf(szTmp, "memory error!");
			OnLogData(5, szTmp);
			return false;
		}
	}
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
	if (_impl->cfg.auto_reconnect) 
		return;
	//
	_impl->loop->remove(_impl->conn);
	_impl->conn->close();
#if _WIN32
	SOCKET sock = _impl->conn->create();
	if(INVALID_SOCKET == sock)
#else 
	int sock = _impl->conn->create();
	if (sock < 0)
#endif 
	{
		return;
	}
	//
	if (!_impl->conn->connect(_impl->host_ip[_impl->ip_idx], _impl->port))
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


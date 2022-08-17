#include "Impl.h"
#include "tcp_conn.h"


bool CBizUser::Impl::init()
{
	if (started)
		return false;
	//port parse 
	char* ptr = strrchr(cfg.url, ':');
	if (ptr == nullptr)
		port = 0;
	else
	{
		*ptr = 0;
		port = atoi(ptr + 1);
	}

	//
	//todo dns parse ip
	//
	memcpy(&cfg, &cfg, sizeof(COMMONCFG));
	//
	if (nullptr == this->loop)
	{
		this->loop = new eventloop;
		if (nullptr == this->loop)
		{
			return false;
		}
	}
	//

	//
	return true;
}

bool CBizUser::Impl::connect()
{
	if (nullptr == conn)
	{
		conn = new tcp_conn(loop, msg_head_fnc, 16, dis_conn_cb, msg_cb);
		if (nullptr == conn)
		{
			char szTmp[1024] = { 0 };
			sprintf(szTmp, "memory error!");
			return false;
		}
	}
}

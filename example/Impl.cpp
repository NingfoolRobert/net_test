#include "Impl.h"
#include "tcp_conn.h"
#include "ngx_log.h"
#include <thread>

#ifdef _WIN32
#include <iphlpapi.h>
#include <conio.h>
#include <assert.h>
#pragma comment(lib, "IPHLPAPI.lib")
#endif 

///////////////////////////////
CBizUser::Impl::Impl(PMSGLENPARSEFUNC head_fnc, PNETMSGCALLBACK msg_cb, PDISCONNCALLBACK dis_cb)
{
	memset(&core, 0, sizeof(core));
	conn = nullptr;

	msg_head_fnc = head_fnc;
	msg_cb = msg_cb;
	dis_conn_cb = dis_conn_cb;
}

//
CBizUser::Impl::~Impl() {
	uninit();	
}

bool CBizUser::Impl::init(const char* name, unsigned int version)
{
	//
	if(name)strcpy(core.api_name, name);
	core.api_version = version;
	if (!ngx_core_init(&core))
	{
		ngx_log_fatal(get_log(), "init %s fail, version:%d.%d, please check congfig...", core.api_name,
			core.api_version & 0xF0 >> 4, core.api_version & 0x0F);
		return false;
	}
	//
	ngx_log_info(get_log(), "init %s success, version:%d.%d...", core.api_name, 
		core.api_version & 0xF0 >> 4, core.api_version & 0x0F);
	return true;
}

bool CBizUser::Impl::connect()
{
	if (nullptr == conn)
	{
		conn = new tcp_conn(16, msg_head_fnc, msg_cb, dis_conn_cb);
		if (nullptr == conn)
		{
			ngx_log_error(core.log, "memory error!");
			return false;
		}
	}
	else
	{
		conn->close();
	}
	//
#ifdef _WIN32 
	SOCKET  fd = conn->create();
	if(INVALID_SOCKET == fd)
#else 
	int fd = conn->create();
	if (fd < 0)
#endif 
	{
		return false;
	}

	if (!conn->connect(core.host_ip[core.ip_idx], core.port[core.ip_idx]))
	{
		ngx_log_error(core.log, "connect server fail., ip:port=%s:", core.cfg.url);
		return false;
	}
	//
	ngx_log_info(core.log,"connect server success, ip:port=%s", core.cfg.url);
	return true;
}


void CBizUser::Impl::uninit()
{
	if (core.loop == NULL)
		return;
	//
	if (conn && conn->_brk_tm == 0)
	{
		conn->terminate();
	}
	//
	ngx_core_uninit(&core);
}

ngx_log* CBizUser::Impl::get_log()
{
	return core.log;
}

bool CBizUser::Impl::logon()
{
	ngx_log_info(get_log(), "start logon server, ip:port=%s:%d...", core.cfg.url);
	//TODO generate log on message;
	return true;
}

void CBizUser::Impl::async(net_io* conn)
{
	if (NULL == conn)
		return;
	conn->set_nio();
	conn->set_tcp_nodelay();
	core.loop->update_event(conn, EV_READ);
}

void CBizUser::Impl::unasync(net_io* conn)
{
	core.loop->update_event(conn, EV_DELETED);
	conn->set_nio(0);
}


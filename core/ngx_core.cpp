#include "ngx_core.h"
#include "eventloop.h"
#include "time.hpp"
#include "net_helper.h"

#include <ctime>
#include <vector>
#include <thread>
#include <string.h>

#ifdef _WIN32
#include <assert.h>
#else 
#include <sys/ioctl.h>
#include <fcntl.h> 
#include <net/if.h> 
#include <sys/socket.h> 
#include <errno.h> 
#include <netdb.h> 
#include <sys/types.h> 
#endif 

#ifdef _WIN32 
#define  ngx_strtok			strtok_s 
#else  
#define  ngx_strtok			strtok_r 
#endif 

#define  LOG_FILE_MAX_SIZE		(100 * 1024 * 1024)

void  active_loop_thread(void* param) {
	// 
	ngx_core_t* core = (ngx_core_t*)param;
	core->started = 1;
	eventloop loop;
	core->loop = &loop;
	while (core->started)
	{
		core->loop->loop(core->timeout);
	}
	core->loop = NULL;
}

void  write_log_timer(void* param) {
	if (NULL == param)
		return;
	ngx_core_t* core = (ngx_core_t*)param;
	unsigned long long  size = ngx_file_size(core->log->_name);
	if (size >= core->log_max_size)
	{
		char szFileName[256] = { 0 };
		struct tm tmNow;
		time_t tNow = time(NULL);
#ifndef _WIN32 
		localtime_r(&tNow, &tmNow);
#else 
		localtime_s(&tmNow, &tNow);
#endif 
		sprintf(szFileName, "./%04d%02d%02d_%02d%02d%02d_%s.log", tmNow.tm_year + 1900,
			tmNow.tm_mon +1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec,
			core->api_name);
		ngx_file_rename(core->log->_name, szFileName);
	}
	//
	core->log->print_log_file(core->log);
}

bool ngx_core_init(ngx_core_t *core)
{
	if (NULL == core)
		return false;
	//
	char szFileFullName[256] = { 0 };
	sprintf(szFileFullName, "./%s.log", core->api_name);
	if (NULL == core->log)
	{
		core->log = new ngx_log(szFileFullName);
		if (core->log == NULL)
		{
			printf("memory error.");
			return false;
		}
	}
	//
	if (core->timeout == 0)			core->timeout = 10;
	if (core->cfg.hearbeat_int == 0) core->cfg.hearbeat_int = 30;
	if (core->cfg.log_level == 0) core->cfg.log_level = log_level_info;
	if (core->log_max_size == 0)	 core->log_max_size = LOG_FILE_MAX_SIZE;
	core->ip_cnt = 0;
	//
	net::helper::get_local_ip(core->host_ip, core->mac, HOST_MAC_MAX);
	char szTmp[256] = { 0 };
	strcpy(szTmp, core->cfg.url);
	const char* splite = ";£»";
	char* pSave = NULL;
	//
	char* ptr = ngx_strtok(szTmp, splite, &pSave);
	while (ptr) {
		//
		int ret = net::helper::parse_url(ptr, core->ip, HOST_IP_MAX);
		int port = 80;
		char* p = strrchr(ptr, ':');
		if (p) {
			*p = 0;
			port = std::atoi(p + 1);
		}
		for (auto i = 0u; i < ret; ++i)
			core->port[core->ip_cnt + i] = port;
		//
		core->ip_cnt += ret;
		ptr = ngx_strtok(nullptr, splite, &pSave);
	}
	
	//
	if (!core->started)
	{
		std::thread thr(&active_loop_thread, core);
		thr.detach();
		//
		timer_info_t tb;
		tb.cb = write_log_timer;
		tb.count = 0;
		tb.gap = 10 * detail::time_unit::NANOSECONDS_PER_SECOND;
		tb.param = core;
		core->loop->add_timer(tb);
	}
	//
	if (core->ip_cnt == 0)
	{
		ngx_log_error(core->log, "not find the server address:%s.", core->cfg.url);
		ngx_core_uninit(core);
		return false;
	}
	//
	return true;
}

void ngx_core_uninit(ngx_core_t *core)
{
	core->started = false;
	core->loop->stop();
	while (core->loop);
	core->log->print_log_file(core->log);
}

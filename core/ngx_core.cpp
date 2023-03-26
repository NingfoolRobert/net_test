#include "ngx_core.h"
#include "eventloop.h"

#include <ctime>
#include <vector>
#include <thread>
#include <string.h>

#ifdef _WIN32
#include <iphlpapi.h>
#include <conio.h>
#include <assert.h>
#pragma comment(lib, "IPHLPAPI.lib")
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

void parse_url(ngx_core_t *core)
{
	if (NULL == core)
		return;
	//
	char szTmp[256];
	struct hostent *hptr;
	strcpy(szTmp, core->cfg.url);
	const char*  splite = ";£»";
	char* pSave = NULL;
#ifndef _WIN32 
	char *ptr = strtok_r(szTmp, splite, &pSave);
#else 
	char* ptr = strtok_s(szTmp, splite, &pSave);
#endif 
	while (ptr)
	{
		char*  pTmp = strrchr(ptr, ':');
		if (pTmp == NULL)
		{
			core->port[core->ip_cnt] = 0;
			hptr = gethostbyname(ptr);
		}
		else
		{
			core->port[core->ip_cnt] = atoi(pTmp + 1);
			*pTmp = 0;
			hptr = gethostbyname(ptr);
		}
		//  DNS => ip
		if (hptr == NULL || hptr->h_addrtype != AF_INET)
			continue;
		//
		for (char** pptr = hptr->h_addr_list; pptr; pptr++)
		{
			core->host_ip[core->ip_cnt++] = ntohl(((struct in_addr*)pptr)->s_addr);
		}

		ptr = ngx_strtok(NULL, splite, &pSave);
	}

}

void get_mac(ngx_core_t *core)
{
#ifdef _WIN32 
	IP_ADAPTER_INFO AdapterInfo[16];			// Allocate information 
// for up to 16 NICs
	DWORD dwBufLen = sizeof(AdapterInfo);		// Save memory size of buffer
	IP_ADAPTER_INFO* pAdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));
	DWORD dwStatus = GetAdaptersInfo(			// Call GetAdapterInfo
		pAdapterInfo,							// [out] buffer to receive data
		&dwBufLen);								// [in] size of receive data buffer
	assert(dwStatus == ERROR_SUCCESS);			// Verify return value is 
	// valid, no buffer overflow
	// Contains pointer to*/
	int  idx = 0;
	for (PIP_ADAPTER_INFO pInfo = pAdapterInfo; pInfo != NULL; pInfo = pInfo->Next)
	{
		if (pAdapterInfo->Type != MIB_IF_TYPE_ETHERNET)
			continue;
		//
		if (idx >= HOST_MAC_MAX) break;
		sprintf(core->mac[idx++], "%02X-%02X-%02X-%02X-%02X-%02X",
			pAdapterInfo->Address[0],
			pAdapterInfo->Address[1],
			pAdapterInfo->Address[2],
			pAdapterInfo->Address[3],
			pAdapterInfo->Address[4],
			pAdapterInfo->Address[5]
		);
	}
	free(pAdapterInfo);
#else 
	int idx = 0;
	struct ifreq ifr{};
	struct ifconf ifc{};
	char buf[1024];
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock == -1) {
		strcpy(core->mac[idx], "n/a");
		return;
	}
	//
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl(sock, SIOCGIFCONF, &ifc) == -1)
	{
		strcpy(core->mac[idx], "n/a");
		return;
	}
	//
	struct ifreq* it = ifc.ifc_req;
	const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
	for (; it != end; it++)
	{
		strcpy(ifr.ifr_name, it->ifr_name);
		if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
		{
			if (!(ifr.ifr_flags & IFF_LOOPBACK))
			{ // don't count loopback
				if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0)
				{
					unsigned char * ptr;
					ptr = (unsigned char  *)&ifr.ifr_ifru.ifru_hwaddr.sa_data[0];
					snprintf(core->mac[idx++], 64, "%02X-%02X-%02X-%02X-%02X-%02X", *ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3), *(ptr + 4), *(ptr + 5));
				}
			}
		}
	}
	//
	if (idx == 0)
		strcpy(core->mac[idx], "00:00:00:00:00:00");
#endif 
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
	parse_url(core);
	//
	get_mac(core);
	//
	if (!core->started)
	{
		std::thread thr(&active_loop_thread, core);
		thr.detach();
		//
		timer_info_t tb;
		tb.cb = write_log_timer;
		tb.count = 0;
		tb.gap = 10;
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
	core->loop->wakeup();
	while (core->loop);
	core->log->print_log_file(core->log);
}

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

//
void write_log_timer_cb(void* param) {
	CBizUser::Impl *impl = (CBizUser::Impl*)param;
	impl->log->print_log_file(impl->log);
}
//

void active_work_thread(void* param) {
	
	std::vector<net_client_base*> vecTmp;
	CBizUser::Impl* impl = (CBizUser::Impl*)param;
	impl->started = true;
	while (impl->started)
	{
		impl->loop->loop(vecTmp, impl->timeout);
	}
	//
	ngx_log_info(impl->log, "%s stoped...", impl->api_name);
}

///////////////////////////////
CBizUser::Impl::Impl():
	loop(nullptr), 
	conn(nullptr), 
	logoned(0), 
	started(0), 
	timeout(10), 
	ip_cnt(0), 
	ip_idx(0)
{

	if (cfg.hearbeat_int == 0) cfg.hearbeat_int = 30;
	if (cfg.log_level == 0) cfg.log_level = 4;
	//
	char szLogFileName[256] = { 0 };
	sprintf(szLogFileName, "./%s.log", api_name);
	this->log = new ngx_log(szLogFileName);
}
//
CBizUser::Impl::~Impl() {
	//
	if (loop)
	{
		delete loop;
		loop = nullptr;
	}
	//
	if (conn)
	{
		delete conn;
		conn = nullptr;
	}
	//
	if (log)
	{
		this->log->print_log_file(this->log);
		delete log;
		log = NULL;
	}
	//
	
}

bool CBizUser::Impl::init()
{
	if (started)
		return false;
	//
	if (nullptr == this->log)
	{
		char szApiFileName[256] = { 0 };
		sprintf(szApiFileName, "./%s.log", this->api_name);
		this->log = new ngx_log(szApiFileName);
		if (this->log == NULL)
			return false;
	}

	//dns parse, host_ip && ip_cnt;
	parse_url();
	if (ip_cnt == 0)
		return false;

	//acquire mac  addr 
	get_local_mac();
	//
	if (nullptr == this->loop)
	{
		this->loop = new eventloop(log);
		if (nullptr == this->loop)
		{
			ngx_log_error(this->log, "memory error.");
			return false;
		}
		//
		tagtimercb time_cb;
		time_cb.cb = write_log_timer_cb;
		time_cb.count = -1;
		time_cb.time_gap = 10;
		time_cb.param = this;
		this->loop->add_timer(time_cb);
	}
	//
	std::thread thr(&active_work_thread, this);
	thr.detach();

	return true;
}

void CBizUser::Impl::parse_url()
{
	//port parse 
	char* ptr = strrchr(cfg.url, ':');
	if (ptr == nullptr)
	{
		port = 0;
	}
	else
	{
		*ptr = 0;
		port = atoi(ptr + 1);
	}
	//
	struct hostent *hptr;
	hptr = gethostbyname(cfg.url);
	if (hptr == NULL)
		return;
	if (hptr->h_addrtype != AF_INET)
		return;
	//
	struct in_addr addr_;
	for (char** pptr = hptr->h_addr_list; pptr; pptr++)
	{
		host_ip[ip_cnt++] = ntohl(((struct in_addr*)pptr)->s_addr);
	}
}

bool CBizUser::Impl::connect()
{
	if (nullptr == conn)
	{
		conn = new tcp_conn(loop, msg_head_fnc, 16, dis_conn_cb, msg_cb);
		if (nullptr == conn)
		{
			ngx_log_error(log, "memory error!");
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

	if (!conn->connect(host_ip[ip_idx], port))
	{
		ngx_log_error(this->log, "connect server fail., ip:port=%s:%d", cfg.url, port);
		return false;
	}
	//
	conn->_break_timestamp = 0;
	ngx_log_info(this->log,"connect server success, ip:port=%s:%d", cfg.url, port);
	return true;
}

void CBizUser::Impl::get_local_mac()
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
		if(pAdapterInfo->Type != MIB_IF_TYPE_ETHERNET)
			continue;
		//
		if (idx >= HOST_MAC_MAX) break;
		sprintf(mac[idx++], "%02X-%02X-%02X-%02X-%02X-%02X",
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
	struct ifreq ifr {};
	struct ifconf ifc {};
	char buf[1024];
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock == -1) {
		strcpy(mac[idx], "n/a");
		return;
	}
	//
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl(sock, SIOCGIFCONF, &ifc) == -1)
	{
		strcpy(mac[idx], "n/a");
		return;
	}
	//
	struct ifreq* it = ifc.ifc_req;
	const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
	for (; it != end; ++it) 
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
					snprintf(mac[idx++], 64, "%02X-%02X-%02X-%02X-%02X-%02X", *ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3), *(ptr + 4), *(ptr + 5));
				}
			}
		}
	}
	//
	if (idx == 0)
		strcpy(mac[idx], "00:00:00:00:00:00");
#endif 
}

void CBizUser::Impl::get_local_ip()
{

}

void CBizUser::Impl::uninit()
{
	if (!started)
		return;
		
	this->started = false;
	//
	this->conn->OnTerminate();
	loop->wakeup();
}


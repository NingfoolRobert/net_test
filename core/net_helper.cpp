#include "net_helper.h"
#ifndef _WIN32 
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/ioctl.h> 
#include <unistd.h> 
#include <net/if.h> 
#include <ifaddrs.h> 
#include <arpa/inet.h> 
#include <fcntl.h> 
#include <errno.h> 
#include <netdb.h> 
#else 
#include <conio.h>
#include <assert.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")
#endif 
#include <string.h>
#include <memory>

#ifdef _WIN32 
#define  ngx_strtok			strtok_s 
#else  
#define  ngx_strtok			strtok_r 
#endif 

namespace net{
	namespace helper{
		int get_local_ip(uint32_t* ip, uint64_t* mac, size_t size) {
			size_t count = 0;
#ifndef _WIN32 
			struct ifreq ifr {};
			struct ifconf ifc {};
			struct sockaddr_in  sadr;
			char buf[1024];
			int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
			if (sock == -1) return -1;
			ifc.ifc_len = sizeof(buf);
			ifc.ifc_buf = buf;
			if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) { close(sock); return -1; }
			//
			struct ifreq* it = ifc.ifc_req;
			const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
			for (; it != end && count < size; ++it) {
				memset(&ifr, 0, sizeof(ifr));
				strncpy(ifr.ifr_name, it->ifr_name, IFNAMSIZ);
				//printf("---\neth:%s\n", ifr.ifr_name);
				if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {// don't count loopback
					if (ifr.ifr_flags & IFF_LOOPBACK)
						continue;
					//ip
					strncpy(ifr.ifr_name, it->ifr_name, IFNAMSIZ);
					if (ioctl(sock, SIOCGIFADDR, &ifr) == 0) {
						memcpy(&sadr, &ifr.ifr_ifru.ifru_addr, sizeof(sadr));
						ip[count] = ntohl(sadr.sin_addr.s_addr);
						//strncpy(ip_buf, inet_ntoa(sadr.sin_addr), ip_buf_size);	
						//printf("ip:%s\n", ip_buf);
					}
					//mac
					strncpy(ifr.ifr_name, it->ifr_name, IFNAMSIZ);
					if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
						unsigned char* ptr;
						ptr = (unsigned char*)&ifr.ifr_ifru.ifru_hwaddr.sa_data[0];
						mac[count] = (uint64_t)ptr[0] << 40;
						mac[count] += (uint64_t)ptr[1] << 32;
						mac[count] += (uint64_t)ptr[2] << 24;
						mac[count] += (uint64_t)ptr[3] << 16;
						mac[count] += (uint64_t)ptr[4] << 8;
						mac[count] += ptr[5];
						//printf("mac:%012lX\n", mac[count]);
						count++;
					}
				}
			}
			close(sock);
#else 
			//PIP_ADAPTER_INFO结构体指针存储本机网卡信息
			PIP_ADAPTER_INFO pIpAdapterInfo = (PIP_ADAPTER_INFO)malloc(sizeof(IP_ADAPTER_INFO));
			//得到结构体大小,用于GetAdaptersInfo参数
			unsigned long stSize = sizeof(IP_ADAPTER_INFO);
			//调用GetAdaptersInfo函数,填充pIpAdapterInfo指针变量;其中stSize参数既是一个输入量也是一个输出量
			int nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);
			//记录网卡数量
			//DWORD netCardNum = 0;
			//GetNumberOfInterfaces(&netCardNum);
			//cout << "网卡数量：" << netCardNum << endl; netCardNum = 0;
			//记录每张网卡上的IP地址数量
			int IPnumPerNetCard = 0;
			if (ERROR_BUFFER_OVERFLOW == nRel)
			{
				//如果函数返回的是ERROR_BUFFER_OVERFLOW
				//则说明GetAdaptersInfo参数传递的内存空间不够,同时其传出stSize,表示需要的空间大小
				//这也是说明为什么stSize既是一个输入量也是一个输出量
				//释放原来的内存空间
				delete pIpAdapterInfo;
				//重新申请内存空间用来存储所有网卡信息
				pIpAdapterInfo = (PIP_ADAPTER_INFO)malloc(stSize);
				//再次调用GetAdaptersInfo函数,填充pIpAdapterInfo指针变量
				nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);
			}
			if (ERROR_SUCCESS != nRel)
				return count;
	
			//多网卡,因此通过循环去判断
			for (auto pInfo = pIpAdapterInfo; pInfo; pInfo = pInfo->Next) {
				if (pInfo->Type == MIB_IF_TYPE_LOOPBACK)
					continue;
				//check adapter avaliable
				MIB_IFROW mi;    // 存放获取到的Adapter参数
				memset(&mi, 0, sizeof(MIB_IFROW));
				mi.dwIndex = pInfo->Index; // dwIndex是需要获取的Adapter的索引，可以通过GetAdaptersInfo和其他相关函数获取
				if (GetIfEntry(&mi) != NOERROR)
					continue;
				//
				if (mi.dwOperStatus == IF_OPER_STATUS_NON_OPERATIONAL ||		//不可用
					mi.dwOperStatus == IF_OPER_STATUS_UNREACHABLE ||
					mi.dwOperStatus == IF_OPER_STATUS_DISCONNECTED ||
					mi.dwOperStatus == IF_OPER_STATUS_CONNECTING)
					continue;
				
				ip[count] = ip_to_host(pInfo->IpAddressList.IpAddress.String);
				mac[count] = 0;
				for (auto i = 0; i < pInfo->AddressLength; ++i) {
					mac[count] <<= 8;
					mac[count] += pInfo->Address[i];
				}
				count++;
			}
			//释放内存空间
			if (pIpAdapterInfo)
			{
				free(pIpAdapterInfo);
				pIpAdapterInfo = NULL;
			}
#endif 
			return count;
		}

		int parse_url(const char* url, uint32_t* ip, size_t iplen) {
			if (NULL == url) return 0;
			//
			size_t count = 0;
			char szTmp[256];
			struct hostent* hptr;
			strcpy(szTmp, url);
			char* pSave = NULL;
			const char* splite = ";";
			char* ptr = ngx_strtok(szTmp, splite, &pSave);
			while (ptr)
			{
				char* pTmp = strrchr(ptr, ':');
				if (pTmp == NULL)
				{
					hptr = gethostbyname(ptr);
				}
				else
				{
					*pTmp = 0;
					hptr = gethostbyname(ptr);
				}
				//  DNS => ip
				if (hptr == NULL || hptr->h_addrtype != AF_INET)
					continue;
				//
				for (char** pptr = hptr->h_addr_list; pptr && count < iplen; pptr++)
				{
					ip[count++] = ntohl(((struct in_addr*)pptr)->s_addr);
				}

				ptr = ngx_strtok(NULL, splite, &pSave);
			}
			return count;
		}

		uint32_t	ip_to_host(const char* ip) {
			unsigned int hostip = 0;
			char* end;
			const char* ptr = ip;
			while (*ptr) {
				int val = strtol(ptr, &end, 10);
				hostip <<= 8;
				hostip += val;
				if (*end == 0)
					break;
				ptr = end + 1;
			}
			return hostip;
		}

		const char* host_to_ip(const uint32_t hostip, char* ip) {
			sprintf(ip, "%d.%d.%d.%d", hostip >> 24, (hostip >> 16) & 0xFF, (hostip >> 8) & 0xFF, hostip & 0xFF);
			return ip;
		}
	}
}

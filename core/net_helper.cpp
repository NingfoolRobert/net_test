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
#endif 
#include <string.h>

#ifdef _WIN32 
#define  ngx_strtok			strtok_s 
#else  
#define  ngx_strtok			strtok_r 
#endif 

namespace net{
	namespace helper{
		int get_local_ip(uint32_t* ip,  uint64_t* mac, size_t& size){
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
				printf("---\neth:%s\n", ifr.ifr_name);
				if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {// don't count loopback
					if (ifr.ifr_flags & IFF_LOOPBACK) 
						continue;
					//ip
					strncpy(ifr.ifr_name, it->ifr_name, IFNAMSIZ);
					if(ioctl(sock, SIOCGIFADDR, &ifr) == 0){
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
						mac[count]  = (uint64_t)ptr[0] << 40;
						mac[count] += (uint64_t)ptr[1] << 32;
					    mac[count] += (uint64_t)ptr[2] << 24;
					    mac[count] += (uint64_t)ptr[3] << 16;
						mac[count] += (uint64_t)ptr[4] << 8;
						mac[count] += ptr[5];
						printf("mac:%012lX\n", mac[count]);
						count++;
					}
				}
			}
			close(sock);
#else 
#endif 
			return count;
		}

		int parse_url(const char* url,  uint32_t* ip,  size_t& iplen){
			if(NULL == url) return 0;
		//
			size_t count = 0;
		char szTmp[256];
		struct hostent *hptr;
		strcpy(szTmp, url);
		const char*  splite = ";；";
		char* pSave = NULL;
	char *ptr = ngx_strtok(szTmp, splite, &pSave);
	while (ptr)
	{
		char*  pTmp = strrchr(ptr, ':');
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


	}
}

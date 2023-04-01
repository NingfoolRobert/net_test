/**
 * @file	net_helper.h
 * @brief 
 * @author	ningbf
 * @version 1.0.0
 * @date	2023-03-30
 */

#ifndef _NET_HELPER_H_
#define _NET_HELPER_H_ 

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

namespace net{
	//
	namespace helper{
		//
		int get_local_ip(uint32_t* ip,  uint64_t* mac,  size_t size);
		//
		int parse_url(const char* url,  uint32_t* ip, size_t iplen);
		
		uint32_t	ip_to_host(const char* ip);

		const char* host_to_ip(const uint32_t host, char* ip);
	}
}



#endif //_NET_HELPER_H_


/**
 * @file net_common.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-02
 */
#pragma once
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <regex>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
#include "net_util.h"

namespace net {
namespace common {

struct addr_t {
    char ip[16];
    unsigned short port = 0;
    unsigned short family = AF_INET;

    addr_t() {
        memset(ip, 0, sizeof(ip));
    }
    //
    void set_ip(const char *ip_) {
        if (nullptr == ip_) {
            return;
        }
        strncpy(ip, ip_, sizeof(ip) - 1);
    }
    //
    void set_ip(std::string &ip_) {
        strncpy(ip, ip_.c_str(), sizeof(ip) - 1);
    }

    void set_port(unsigned short port_) {
        port = port_;
    }

    struct sockaddr_in to_sockaddr() {
        struct sockaddr_in sock_addr;
        memset(&sock_addr, 0, sizeof(sock_addr));
        sock_addr.sin_family = family;
        sock_addr.sin_port = htons(port);
        sock_addr.sin_addr.s_addr = inet_addr(ip);
        return sock_addr;
    }

    void from_sockaddr(struct sockaddr_in &sock_addr) {
        family = sock_addr.sin_family;
        port = ntohs(sock_addr.sin_port);
        strncpy(ip, inet_ntoa(sock_addr.sin_addr), sizeof(ip) - 1);
    }

    std::string to_string() {
        std::string str = ip;
        str += ":";
        return str + std::to_string(port);
    }
};

struct ip_pair {
    addr_t dst;
    addr_t src;
    unsigned short protocol;
    short if_idx = -1;

    void set_if_name(const char *if_name) {
        std::regex pattern(R"(\d+\.\d+\.\d+\.\d+)");
        std::string ifn = if_name;
        if (std::regex_match(ifn, pattern)) {
          ifn = net::util::ipv4_to_if_name(if_name);
          if_idx = if_nametoindex(ifn.c_str());
        }
        else {
            if_idx = if_nametoindex(if_name);
        }
      
        assert(if_idx != 0);
    }

    std::string get_if_name() {
        if (if_idx == -1 || if_idx == 0) {
            return "";
        }
        return  net::util::if_index_to_name(if_idx);
    }
};

}  // namespace common
}  // namespace net

/**
 * @file net_common.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-02
 */
#pragma once
#include "net_util.h"
#include <arpa/inet.h>
#include <assert.h>
#include <bits/types/struct_timeval.h>
#include <cstdint>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <regex>
#include <string>
#include <sys/socket.h>

namespace net {

struct addr_t {
  char     ip[16]{};
  uint16_t port = 0;

  addr_t() {
    memset(ip, 0, sizeof(addr_t));
  }
  //
  void set_ip(const char *ip_) {
    if (nullptr == ip_) {
      return;
    }
    strncpy(ip, ip_, sizeof(ip) - 1);
  }

  void set_net_ip(uint32_t net_ip) {
    inet_ntop(AF_INET, &net_ip, ip, sizeof(ip));
  }
  //
  void set_ip(std::string &ip_) {
    strncpy(ip, ip_.c_str(), sizeof(ip) - 1);
  }

  void set_port(uint16_t port_) {
    port = port_;
  }

  void set_net_port() {
    port = ntohs(port);
  }

  struct sockaddr_in to_sockaddr() {
    struct sockaddr_in sock_addr{};
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family      = AF_INET;
    sock_addr.sin_port        = htons(port);
    sock_addr.sin_addr.s_addr = inet_addr(ip);
    return sock_addr;
  }

  void from_sockaddr(struct sockaddr_in &sock_addr) {
    port = ntohs(sock_addr.sin_port);
    strncpy(ip, inet_ntoa(sock_addr.sin_addr), sizeof(ip) - 1);
  }

  std::string to_string() {
    std::string str = ip;
    str += ":";
    return str + std::to_string(port);
  }
};

struct ip_pair_t {
  addr_t   dst;
  addr_t   src;
  uint16_t protocol = IPPROTO_UDP;
  uint16_t if_idx   = -1;

  void set_if_name(const char *if_name) {
    std::regex  pattern(R"(\d+\.\d+\.\d+\.\d+)");
    std::string ifn = if_name;
    if (std::regex_match(ifn, pattern)) {
      ifn    = net::util::ipv4_to_if_name(if_name);
      if_idx = if_nametoindex(ifn.c_str());
    }
    else {
      if_idx = if_nametoindex(if_name);
    }

    assert(if_idx != 0);
  }

  std::string get_if_name() const {
    if (if_idx == -1 || if_idx == 0) {
      return "";
    }
    return net::util::if_index_to_name(if_idx);
  }
};

struct pkt_info_t {
  ip_pair_t      pair;
  struct timeval tv{};
};

struct endpoint_t {
  ip_pair_t pair;
  void     *handle = nullptr;
};

}  // namespace net

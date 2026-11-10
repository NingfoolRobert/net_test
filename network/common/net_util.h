/**
 * @file net_util.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-04
 */
#pragma once

#include <arpa/inet.h>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {
namespace util {

inline std::string ipv4_to_if_name(const char *ipv4) {

  struct ifaddrs *ifaddr = nullptr;
  struct ifaddrs *ifa = nullptr;
  int             family = 0;
  if (getifaddrs(&ifaddr) == -1) {
    return "";
  }

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (nullptr == ifa->ifa_addr) {
      continue;
    }

    family = ifa->ifa_addr->sa_family;
    //
    if (family != AF_INET) {
      continue;
    }
    //
    char host[256];
    if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, 256, nullptr, 0, NI_NUMERICHOST) == 0) {
      if (strcmp(ipv4, host) == 0) {
        return ifa->ifa_name;
      }
    }
  }
  return "";
}

inline std::string if_name_to_ipv4(const char *if_name) {

  struct ifaddrs *ifaddr = nullptr;
  struct ifaddrs *ifa = nullptr;
  int             family = 0;
  if (getifaddrs(&ifaddr) == -1) {
    return "";
  }

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (nullptr == ifa->ifa_addr) {
      continue;
    }

    family = ifa->ifa_addr->sa_family;
    //
    if (family != AF_INET) {
      continue;
    }
    //
    char host[256];
    if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, 256, nullptr, 0, NI_NUMERICHOST) == 0) {
      if (strcmp(if_name, ifa->ifa_name) == 0) {
        return host;
      }
    }
  }
  return "";
}

inline std::string if_index_to_name(int idx) {
  struct ifreq ifr{};
  memset(&ifr, 0, sizeof(ifr));

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    return "";
  }

  ifr.ifr_ifindex = idx;
  if (ioctl(sock, SIOCGIFNAME, &ifr) < 0) {
    close(sock);
    return "";
  }

  close(sock);
  return ifr.ifr_name;
}

inline bool is_multicast_addr(const char *ipv4) {
  if (nullptr == ipv4) {
    return false;
  }
  //
  struct in_addr addr{};
  addr.s_addr = inet_addr(ipv4);
  uint32_t ip = ntohl(addr.s_addr);
  return (ip >= 0xE0000000) && (ip <= 0xEFFFFFFF);
}

inline uint32_t ipv4_to_host_ip(const char *ip) {
  if (nullptr == ip) {
    return 0;
  }
  //
  uint32_t net_ip = inet_addr(ip);
  return htonl(net_ip);
}

inline bool is_multicast_ipv4(const char *ipv4) {
  if (nullptr == ipv4) {
    return false;
  }
  //
  auto ip = ipv4_to_host_ip(ipv4);
  return (ip >= 0xE0000000) && (ip <= 0xEFFFFFFF);
}

inline bool is_ipv4_addr(const char *ipv4) {
  if (nullptr == ipv4) {
    return false;
  }

  struct in_addr addr{};
  return inet_pton(AF_INET, ipv4, &addr) == 1;
}
}  // namespace util
}  // namespace net

/**
 * @file bsd_mcbc.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2026-09-12
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "common/net_types.h"
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "socket.h"

namespace net {
struct bsd_mcbc_cfg_t {
  addr_t remote_addr{};
  addr_t local_addr{};
  bool  is_join_group = false;
  bool   is_busy_waiting = false;
  bool   is_multicast    = false;
  char   interface[16]   = {0};
};

class bsd_mcbc_t {
  public:
  explicit bsd_mcbc_t(const bsd_mcbc_cfg_t &cfg) : cfg_(cfg) {
  }

  bsd_mcbc_t(const bsd_mcbc_t &)            = delete;
  bsd_mcbc_t &operator=(const bsd_mcbc_t &) = delete;

  ~bsd_mcbc_t() {
    close();
  }

  bool bind() {
    if (!open_socket()) {
      return false;
    }

    if (socket_.setsockopt(SOL_SOCKET, SO_REUSEADDR, &enabled_, sizeof(enabled_)) < 0 ||
        socket_.setsockopt(SOL_SOCKET, SO_BROADCAST, &enabled_, sizeof(enabled_)) < 0 ||
        socket_.bind(&cfg_.local_addr) < 0) {
      close();
      return false;
    }

    if (cfg_.is_multicast) {
      ip_mreq mreq{};
      mreq.imr_multiaddr.s_addr = inet_addr(cfg_.remote_addr.ip);
      mreq.imr_interface.s_addr = cfg_.interface[0] == '\0' ? htonl(INADDR_ANY) : inet_addr(cfg_.interface);
      if (socket_.setsockopt(IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        close();
        return false;
      }
      joined_group_ = true;
    }

    if (!set_nonblocking()) {
      close();
      return false;
    }
    return true;
  }

  bool connect() {
    if (!open_socket()) {
      return false;
    }

    if (cfg_.local_addr.ip[0] != '\0' || cfg_.local_addr.port != 0) {
      if (socket_.bind(&cfg_.local_addr) < 0) {
        close();
        return false;
      }
    }

    if (cfg_.is_multicast && cfg_.interface[0] != '\0') {
      in_addr interface_addr{};
      interface_addr.s_addr = inet_addr(cfg_.interface);
      if (socket_.setsockopt(IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) < 0) {
        close();
        return false;
      }
    }

    if (socket_.connect(cfg_.remote_addr) < 0 || !set_nonblocking()) {
      close();
      return false;
    }
    return true;
  }

  void close() {
    if (!socket_.is_invalid_socket() && joined_group_) {
      ip_mreq mreq{};
      mreq.imr_multiaddr.s_addr = inet_addr(cfg_.remote_addr.ip);
      mreq.imr_interface.s_addr = cfg_.interface[0] == '\0' ? htonl(INADDR_ANY) : inet_addr(cfg_.interface);
      socket_.setsockopt(IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
      joined_group_ = false;
    }
    socket_.close();
  }

  int recv(char *data, int len) {
    return socket_.recv(data, len, 0);
  }

  int send(const char *data, int len) {
    sockaddr_in destination = cfg_.remote_addr.to_sockaddr();
    return socket_.sendto(
        data, static_cast<size_t>(len), reinterpret_cast<sockaddr *>(&destination), sizeof(destination), 0);
  }

  const socket_t &socket() const {
    return socket_;
  }

  private:
  bool open_socket() {
    if (!socket_.is_invalid_socket() || cfg_.remote_addr.port == 0 || !util::is_ipv4_addr(cfg_.remote_addr.ip) ||
        (cfg_.local_addr.ip[0] != '\0' && !util::is_ipv4_addr(cfg_.local_addr.ip)) ||
        (cfg_.interface[0] != '\0' && !util::is_ipv4_addr(cfg_.interface)) ||
        (cfg_.is_multicast && !util::is_multicast_ipv4(cfg_.remote_addr.ip))) {
      return false;
    }

    if (socket_.open(AF_INET, SOCK_DGRAM, IPPROTO_UDP) < 0) {
      return false;
    }
    if (socket_.setsockopt(SOL_SOCKET, SO_REUSEADDR, &enabled_, sizeof(enabled_)) < 0 ||
        socket_.setsockopt(SOL_SOCKET, SO_BROADCAST, &enabled_, sizeof(enabled_)) < 0) {
      close();
      return false;
    }
    return true;
  }

  bool set_nonblocking() {
    if (!cfg_.is_busy_waiting) {
      return true;
    }
    int enabled = 1;
    return socket_.ioctl(FIONBIO, &enabled) == 0;
  }

  socket_t       socket_;
  bsd_mcbc_cfg_t cfg_;
  bool           joined_group_ = false;
  int            enabled_      = 1;
};
}  // namespace net

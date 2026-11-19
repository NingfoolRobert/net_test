/**
 * @file rx_bsd_tcp.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-01
 */
#pragma once

#include "common/net_types.h"
#include "socket.h"
#include <sys/socket.h>

namespace net {
struct bsd_tcp_cfg_t {
  addr_t local_addr{};
  addr_t remote_addr{};
  bool   is_busy_waiting = false;
  int    backlog         = 10;
};

class bsd_tcp_t {
  public:
  explicit bsd_tcp_t(const bsd_tcp_cfg_t &cfg);

  bsd_tcp_t(socket_t sock, const bsd_tcp_cfg_t &cfg) : socket_(sock), cfg_(cfg) {
  }

  void close() {
    socket_.close();
  }

  int recv(char *data, int len) {
    return socket_.recv(data, len, 0);
  }

  int send(const char *data, int len) {
    return socket_.send(data, len, 0);
  }

  socket_t socket() const {
    return socket_;
  }

  bsd_tcp_t *accept() {
    if (socket_.is_invalid_socket()) {
      return nullptr;
    }

    socket_t client_socket = socket_.accept();
    if (client_socket.is_invalid_socket()) {
      return nullptr;
    }

    return new bsd_tcp_t(client_socket, cfg_);
  }

  bool bind() {
    if (socket_.open(AF_INET, SOCK_STREAM, IPPROTO_TCP) < 0) {
      return false;
    }

    if (socket_.bind(&cfg_.local_addr) == 0) {
      return false;
    }

    if (cfg_.is_busy_waiting) {
      int flags = 1;
      socket_.ioctl(FIONBIO, &flags);
    }

    if (socket_.listen(cfg_.backlog) == 0) {
      return false;
    }

    return true;
  }

  bool connect() {
    if (socket_.open(AF_INET, SOCK_STREAM, IPPROTO_TCP) < 0) {
      return false;
    }

    if (socket_.connect(cfg_.remote_addr) == 0) {
      return false;
    }

    if (cfg_.is_busy_waiting) {
      int flags = 1;
      socket_.ioctl(FIONBIO, &flags);
    }

    return true;
  }

  private:
  socket_t      socket_;
  bsd_tcp_cfg_t cfg_;
};
}  // namespace net

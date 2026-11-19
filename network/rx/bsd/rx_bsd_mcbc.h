/**
 * @file rx_bsd_mcbc.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-01
 */
#pragma once

#include "net_types.h"
#include "rx/bsd/bsd_mcbc.h"
#include "rx/bsd/bsd_tcp.h"
#include <sys/socket.h>
#include <unistd.h>

namespace net {

class rx_bsd_mcbc {
  public:
  explicit rx_bsd_mcbc(const bsd_mcbc_cfg_t &cfg);
  bool open() {
    return sock_.bind();
  }

  void close() {
    sock_.close();
  }

  int recvfrom(ip_pair_t *endpoint, char *data, int len) {
    endpoint->dst.set_ip(cfg_.remote_addr.ip);
    endpoint->dst.set_port(cfg_.remote_addr.port);
    return sock_.recv(data, len);
  }

  private:
  bsd_mcbc_cfg_t cfg_;
  bsd_mcbc_t     sock_;
};

}  // namespace net

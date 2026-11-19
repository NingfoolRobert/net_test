/**
 * @file tx_bsd_mcbc.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2026-09-13
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "bsd_mcbc.h"
namespace net {

class tx_bsd_mcbc {

  public:
  explicit tx_bsd_mcbc(bsd_mcbc_cfg_t &cfg) : sock_(cfg) {
  }
  bool open() {
    return sock_.connect();
  }

  void close() {
    sock_.close();
  }

  int sendto(const char *data, int len) {
    return sock_.send(data, len);
  }

  private:
  bsd_mcbc_t sock_;
};
}  // namespace net
/**
 * @file rx_bsd_tcp.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2026-09-13
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

namespace net {



class rx_bsd_tcp {
  public:
  bool open();

  void close();

  int recvfrom(endpoint_t* endpoint, char *data, int len);

  private:
};
}  // namespace net
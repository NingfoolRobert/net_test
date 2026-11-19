/**
 * @file rx_pcap_mac.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2026-09-09
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

namespace net {
class rx_pcap_mac {
  public:
  rx_pcap_mac()                               = default;
  ~rx_pcap_mac()                              = default;
  rx_pcap_mac(const rx_pcap_mac &)            = delete;
  rx_pcap_mac &operator=(const rx_pcap_mac &) = delete;

  bool open();

  void close();

  int read(char *data, int len) {
    return 0;
  }
  
};
}  // namespace net
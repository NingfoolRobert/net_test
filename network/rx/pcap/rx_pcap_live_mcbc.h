#pragma once

#include "rx_pcap_mcbc.h"

namespace net {

template <bool ip_reass>
class rx_pcap_live_mcbc : public rx_pcap_mcbc<ip_reass> {
  public:
  bool open();
};

}  // namespace net

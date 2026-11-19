/**
 * @file rx_pcap_mcbc.h
 * @brief
 * @author  bfning
 * @version 0.1
 * @date 2026-09-09
 */
#pragma once

#include "../../common/net_types.h"
#include "ip_reassembly.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct pcap;

namespace net {

struct rx_pcap_mcbc_cfg_t {
  std::vector<ip_pair_t> ip_pairs;
  std::string            file_path;
  size_t                 max_reassembled_pkt_size = 65535;
  uint32_t               reass_timeout_ms         = 3000;
  size_t                 max_reass_entries        = 1024;
  size_t                 max_reass_bytes          = 16 * 1024 * 1024;
};

template <bool ip_reass>
class rx_pcap_mcbc {
  public:
  rx_pcap_mcbc() = default;
  ~rx_pcap_mcbc();
  rx_pcap_mcbc(const rx_pcap_mcbc &)            = delete;
  rx_pcap_mcbc &operator=(const rx_pcap_mcbc &) = delete;

  const rx_pcap_mcbc_cfg_t &config() const {
    return cfg_;
  }

  void set_config(const rx_pcap_mcbc_cfg_t &cfg) {
    cfg_ = cfg;
  }

  bool open();

  void close();

  int read(char *data, int len);

  int read_from(pkt_info_t &pkt, char *data, int len);

  protected:
  bool open_handle(::pcap *handle);

  private:
  rx_pcap_mcbc_cfg_t cfg_;
  ::pcap            *pcap_{nullptr};
  ip_reassembly     reassembly_;

  int errno_{0};
};
}  // namespace net

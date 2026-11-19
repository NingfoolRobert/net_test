#include "rx_pcap_live_mcbc.h"

#include <pcap/pcap.h>

namespace net {
namespace {

constexpr int kSnapLength = 65535;
constexpr int kReadTimeoutMs = 1000;

}  // namespace

template <bool ip_reass>
bool rx_pcap_live_mcbc<ip_reass>::open() {
  this->close();

  const auto &cfg = this->config();
  if (cfg.ip_pairs.empty() || !cfg.file_path.empty() || cfg.max_reassembled_pkt_size == 0 ||
      cfg.max_reassembled_pkt_size > 65535) {
    return false;
  }

  char error[PCAP_ERRBUF_SIZE] = {};
  std::string device_name;
  const int if_idx = cfg.ip_pairs.front().if_idx;

  if (if_idx > 0) {
    device_name = util::if_index_to_name(if_idx);
  }
  else {
    pcap_if_t *devices = nullptr;
    if (pcap_findalldevs(&devices, error) != 0) {
      return false;
    }

    const pcap_if_t *device = nullptr;
    for (const pcap_if_t *candidate = devices; candidate != nullptr; candidate = candidate->next) {
      if ((candidate->flags & PCAP_IF_LOOPBACK) == 0) {
        device = candidate;
        break;
      }
    }
    if (device == nullptr) {
      device = devices;
    }
    if (device != nullptr && device->name != nullptr) {
      device_name = device->name;
    }
    pcap_freealldevs(devices);
  }

  if (device_name.empty()) {
    return false;
  }

  pcap_t *handle = pcap_open_live(device_name.c_str(), kSnapLength, 1, kReadTimeoutMs, error);
  if (handle == nullptr) {
    return false;
  }
  if (!this->open_handle(handle)) {
    pcap_close(handle);
    return false;
  }
  return true;
}

template class rx_pcap_live_mcbc<false>;
template class rx_pcap_live_mcbc<true>;

}  // namespace net

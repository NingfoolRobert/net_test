#include "rx_pcap_mcbc.h"

#include "common/net_types.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <vector>

#include <pcap/pcap.h>

namespace net {
namespace {

constexpr int    kMaxVlanDepth       = 2;
constexpr size_t kMaxIpv4PacketSize = 65535;

uint16_t read_be16(const u_char *data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]));
}

bool locate_ipv4_packet(const u_char *packet, size_t caplen, int datalink, size_t &ip_offset) {
  size_t   offset     = 0;
  uint16_t ether_type = 0;

  switch (datalink) {
    case DLT_EN10MB:
      if (caplen < 14) {
        return false;
      }
      offset     = 14;
      ether_type = read_be16(packet + 12);
      while (ether_type == 0x8100 || ether_type == 0x88a8 || ether_type == 0x9100) {
        if (caplen < offset + 4) {
          return false;
        }
        ether_type = read_be16(packet + offset + 2);
        offset += 4;
      }
      if (ether_type != ETHERTYPE_IP) {
        return false;
      }
      break;
    case DLT_RAW:
      break;
    case DLT_LINUX_SLL:
      if (caplen < 16 || read_be16(packet + 14) != ETHERTYPE_IP) {
        return false;
      }
      offset = 16;
      break;
    case DLT_LINUX_SLL2:
      if (caplen < 20 || read_be16(packet) != ETHERTYPE_IP) {
        return false;
      }
      offset = 20;
      break;
    default:
      return false;
  }

  if (caplen < offset + 20 || (packet[offset] >> 4) != 4) {
    return false;
  }
  ip_offset = offset;
  return true;
}

bool locate_udp_packet(
    const u_char *packet, size_t caplen, int datalink, size_t &ip_offset, size_t &udp_offset, size_t &payload_len) {
  if (!locate_ipv4_packet(packet, caplen, datalink, ip_offset)) {
    return false;
  }

  const size_t ip_header_len = static_cast<size_t>(packet[ip_offset] & 0x0f) * 4;
  if (ip_header_len < 20 || caplen < ip_offset + ip_header_len + 8 || packet[ip_offset + 9] != IPPROTO_UDP) {
    return false;
  }

  udp_offset              = ip_offset + ip_header_len;
  const size_t udp_length = read_be16(packet + udp_offset + 4);
  if (udp_length < 8 || caplen < udp_offset + udp_length) {
    return false;
  }

  payload_len = udp_length - 8;
  return true;
}

std::string make_pair_filter(const ip_pair_t &pair, bool allow_fragments) {
  std::vector<std::string> conditions;
  conditions.emplace_back(allow_fragments ? "ip" : "udp");

  if (pair.src.ip[0] != '\0') {
    conditions.emplace_back(std::string("src host ") + pair.src.ip);
  }
  if (pair.dst.ip[0] != '\0') {
    conditions.emplace_back(std::string("dst host ") + pair.dst.ip);
  }
  if (!allow_fragments && pair.src.port != 0) {
    conditions.emplace_back("src port " + std::to_string(pair.src.port));
  }
  if (!allow_fragments && pair.dst.port != 0) {
    conditions.emplace_back("dst port " + std::to_string(pair.dst.port));
  }

  std::ostringstream base_expression;
  base_expression << '(';
  for (size_t i = 0; i < conditions.size(); ++i) {
    if (i != 0) {
      base_expression << " and ";
    }
    base_expression << conditions[i];
  }
  base_expression << ')';

  // The vlan qualifier adjusts the offsets used by the following protocol
  // qualifiers. Include untagged, single-tagged, and QinQ packets so the
  // filter works for both ordinary Ethernet and VLAN interfaces.
  std::ostringstream expression;
  expression << '(' << base_expression.str() << ')';
  for (int depth = 1; depth <= kMaxVlanDepth; ++depth) {
    expression << " or (";
    for (int tag = 0; tag < depth; ++tag) {
      expression << "vlan and ";
    }
    expression << base_expression.str() << ')';
  }
  return expression.str();
}

std::string make_filter(const std::vector<ip_pair_t> &pairs, bool allow_fragments) {
  std::ostringstream expression;
  for (size_t i = 0; i < pairs.size(); ++i) {
    if (i != 0) {
      expression << " or ";
    }
    expression << make_pair_filter(pairs[i], allow_fragments);
  }
  return expression.str();
}

bool ip_pair_matches(const std::vector<ip_pair_t> &pairs,
                     uint32_t src_ip,
                     uint32_t dst_ip,
                     uint16_t src_port,
                     uint16_t dst_port) {
  char src_text[INET_ADDRSTRLEN] = {};
  char dst_text[INET_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET, &src_ip, src_text, sizeof(src_text)) == nullptr ||
      inet_ntop(AF_INET, &dst_ip, dst_text, sizeof(dst_text)) == nullptr) {
    return false;
  }

  for (const auto &pair : pairs) {
    if (pair.protocol != 0 && pair.protocol != IPPROTO_UDP) {
      continue;
    }
    if (pair.src.ip[0] != '\0' && std::strcmp(pair.src.ip, src_text) != 0) {
      continue;
    }
    if (pair.dst.ip[0] != '\0' && std::strcmp(pair.dst.ip, dst_text) != 0) {
      continue;
    }
    if (pair.src.port != 0 && pair.src.port != src_port) {
      continue;
    }
    if (pair.dst.port != 0 && pair.dst.port != dst_port) {
      continue;
    }
    return true;
  }
  return false;
}

bool parse_ipv4_fragment(const u_char *packet,
                         size_t caplen,
                         int datalink,
                         ipv4_fragment_view &fragment) {
  size_t ip_offset = 0;
  if (!locate_ipv4_packet(packet, caplen, datalink, ip_offset)) {
    return false;
  }
  const size_t ip_header_len = static_cast<size_t>(packet[ip_offset] & 0x0f) * 4;
  if (ip_header_len < 20 || caplen < ip_offset + ip_header_len) {
    return false;
  }
  const uint16_t total_len = read_be16(packet + ip_offset + 2);
  if (total_len < ip_header_len || caplen < ip_offset + total_len) {
    return false;
  }
  std::memcpy(&fragment.key.src_ip, packet + ip_offset + 12, sizeof(fragment.key.src_ip));
  std::memcpy(&fragment.key.dst_ip, packet + ip_offset + 16, sizeof(fragment.key.dst_ip));
  fragment.key.identification = read_be16(packet + ip_offset + 4);
  fragment.key.protocol = packet[ip_offset + 9];
  const uint16_t fragment_field = read_be16(packet + ip_offset + 6);
  fragment.offset_bytes = static_cast<size_t>(fragment_field & 0x1fffU) * 8U;
  fragment.more_fragments = (fragment_field & 0x2000U) != 0;
  fragment.ip_header_length = ip_header_len;
  fragment.payload = packet + ip_offset + ip_header_len;
  fragment.payload_size = total_len - ip_header_len;
  return fragment.key.protocol == IPPROTO_UDP;
}

}  // namespace

template <bool ip_reass>
rx_pcap_mcbc<ip_reass>::~rx_pcap_mcbc() {
  close();
}

template <bool ip_reass>
bool rx_pcap_mcbc<ip_reass>::open() {
  close();
  if (cfg_.ip_pairs.empty() || cfg_.file_path.empty() || cfg_.max_reassembled_pkt_size == 0 ||
      cfg_.max_reassembled_pkt_size > kMaxIpv4PacketSize) {
    return false;
  }

  char        error[PCAP_ERRBUF_SIZE] = {};
  pcap_t *handle = pcap_open_offline(cfg_.file_path.c_str(), error);
  if (handle == nullptr) {
    return false;
  }

  if (!open_handle(handle)) {
    pcap_close(handle);
    return false;
  }
  return true;
}

template <bool ip_reass>
bool rx_pcap_mcbc<ip_reass>::open_handle(::pcap *raw_handle) {
  pcap_t *handle = reinterpret_cast<pcap_t *>(raw_handle);
  if (handle == nullptr) {
    return false;
  }
  if constexpr (ip_reass) {
    reassembly_.configure(cfg_.max_reassembled_pkt_size, cfg_.reass_timeout_ms,
                           cfg_.max_reass_entries, cfg_.max_reass_bytes);
  }

  const std::string bpf = make_filter(cfg_.ip_pairs, ip_reass);
  bpf_program       filter{};
  if (pcap_compile(handle, &filter, bpf.c_str(), 1, PCAP_NETMASK_UNKNOWN) != 0) {
    pcap_close(handle);
    return false;
  }

  const int filter_result = pcap_setfilter(handle, &filter);
  pcap_freecode(&filter);
  if (filter_result != 0) {
    pcap_close(handle);
    return false;
  }

  pcap_ = handle;
  return true;
}

template <bool ip_reass>
void rx_pcap_mcbc<ip_reass>::close() {
  if (pcap_ != nullptr) {
    pcap_close(reinterpret_cast<pcap_t *>(pcap_));
    pcap_ = nullptr;
  }
  reassembly_.clear();
}

template <bool ip_reass>
int rx_pcap_mcbc<ip_reass>::read(char *data, int len) {
  pkt_info_t info;
  return read_from(info, data, len);
}

template <bool ip_reass>
int rx_pcap_mcbc<ip_reass>::read_from(pkt_info_t &pkt, char *data, int len) {

  if (pcap_ == nullptr || data == nullptr || len <= 0) {
    errno_ = EPERM;
    return -1;
  }

  const int datalink = pcap_datalink(reinterpret_cast<pcap_t *>(pcap_));

  for (;;) {
    pcap_pkthdr  *header = nullptr;
    const u_char *packet = nullptr;
    const int result = pcap_next_ex(reinterpret_cast<pcap_t *>(pcap_), &header, &packet);
    if (result <= 0) {
      // 0 is a timeout; -1 is a capture error; -2 means EOF.
      return result;
    }
    if (header == nullptr || packet == nullptr) {
      continue;
    }

    if constexpr (!ip_reass) {
      size_t ip_offset = 0;
      size_t udp_offset = 0;
      size_t payload_len = 0;
      if (!locate_udp_packet(packet, header->caplen, datalink, ip_offset, udp_offset, payload_len)) {
        continue;
      }

      uint32_t src_ip = 0;
      uint32_t dst_ip = 0;
      std::memcpy(&src_ip, packet + ip_offset + 12, sizeof(src_ip));
      std::memcpy(&dst_ip, packet + ip_offset + 16, sizeof(dst_ip));
      const uint16_t src_port = read_be16(packet + udp_offset);
      const uint16_t dst_port = read_be16(packet + udp_offset + 2);
      if (!ip_pair_matches(cfg_.ip_pairs, src_ip, dst_ip, src_port, dst_port)) {
        continue;
      }

      pkt.pair = {};
      pkt.pair.src.set_net_ip(src_ip);
      pkt.pair.src.set_port(src_port);
      pkt.pair.dst.set_net_ip(dst_ip);
      pkt.pair.dst.set_port(dst_port);
      pkt.pair.protocol = IPPROTO_UDP;
      pkt.tv = header->ts;

      const int copied = std::min<int>(len, static_cast<int>(payload_len));
      std::memcpy(data, packet + udp_offset + 8, static_cast<size_t>(copied));
      return copied;
    }

    ipv4_fragment_view fragment;
    if (!parse_ipv4_fragment(packet, header->caplen, datalink, fragment)) {
      continue;
    }
    fragment.timestamp = header->ts;

    reassembled_ip_packet reassembled;
    if (reassembly_.push(fragment, reassembled) != ip_reassembly_result::complete) {
      continue;
    }

    if (reassembled.payload_size < 8) {
      continue;
    }
    const uint16_t udp_length = read_be16(reassembled.payload + 4);
    if (udp_length < 8 || udp_length > reassembled.payload_size) {
      continue;
    }
    const uint16_t src_port = read_be16(reassembled.payload);
    const uint16_t dst_port = read_be16(reassembled.payload + 2);
    if (!ip_pair_matches(cfg_.ip_pairs, reassembled.src_ip, reassembled.dst_ip, src_port, dst_port)) {
      continue;
    }

    pkt.pair = {};
    pkt.pair.src.set_net_ip(reassembled.src_ip);
    pkt.pair.src.set_port(src_port);
    pkt.pair.dst.set_net_ip(reassembled.dst_ip);
    pkt.pair.dst.set_port(dst_port);
    pkt.pair.protocol = IPPROTO_UDP;
    pkt.tv = reassembled.timestamp;
    const size_t payload_len = udp_length - 8;
    const int copied = std::min<int>(len, static_cast<int>(payload_len));
    std::memcpy(data, reassembled.payload + 8, static_cast<size_t>(copied));
    return copied;
  }
}

template class rx_pcap_mcbc<false>;
template class rx_pcap_mcbc<true>;

}  // namespace net

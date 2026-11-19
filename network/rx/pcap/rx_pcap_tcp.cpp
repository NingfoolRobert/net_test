#include "rx_pcap_tcp.h"

#include <pcap/dlt.h>
#include <pcap/pcap.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "../../common/tcp_four_tuple_filter.h"
#include "../../common/tcp_stream_manager.h"
#include "../../common/tcp_stream_reassembly.h"

namespace net {
namespace {

constexpr uint16_t kIpv4EtherType = 0x0800;
constexpr uint8_t kTcpProtocol = 6;

struct ipv4_packet_view {
  uint32_t src_ip = 0;
  uint32_t dst_ip = 0;
  byte_view payload;
};

uint16_t read_be16(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

uint32_t read_be32(const uint8_t* data) {
  return (static_cast<uint32_t>(read_be16(data)) << 16) | read_be16(data + 2);
}

uint32_t checksum_sum(const uint8_t* data, size_t size, uint32_t sum = 0) {
  while (size >= 2) {
    sum += read_be16(data);
    data += 2;
    size -= 2;
  }
  if (size != 0) {
    sum += static_cast<uint32_t>(data[0]) << 8;
  }
  return sum;
}

bool valid_checksum(uint32_t sum) {
  while ((sum >> 16) != 0) {
    sum = (sum & 0xffffU) + (sum >> 16);
  }
  return sum == 0xffffU;
}

bool supported_datalink(int datalink) {
  return datalink == DLT_EN10MB || datalink == DLT_RAW ||
         datalink == DLT_LINUX_SLL || datalink == DLT_LINUX_SLL2;
}

bool locate_ip(const uint8_t* data, size_t size, int datalink, size_t& offset) {
  uint16_t type = kIpv4EtherType;
  switch (datalink) {
    case DLT_EN10MB:
      if (size < 14) {
        return false;
      }
      type = read_be16(data + 12);
      offset = 14;
      break;
    case DLT_LINUX_SLL:
      if (size < 16) {
        return false;
      }
      type = read_be16(data + 14);
      offset = 16;
      break;
    case DLT_LINUX_SLL2:
      if (size < 20) {
        return false;
      }
      type = read_be16(data);
      offset = 20;
      break;
    case DLT_RAW:
      offset = 0;
      break;
    default:
      return false;
  }
  for (int depth = 0; type == 0x8100 || type == 0x88a8 || type == 0x9100;
       ++depth) {
    if (depth == 2 || size - offset < 4) {
      return false;
    }
    type = read_be16(data + offset + 2);
    offset += 4;
  }
  return type == kIpv4EtherType && size - offset >= 20 &&
         (data[offset] >> 4) == 4;
}

bool parse_ip(const uint8_t* data, size_t size, int datalink,
              bool verify_checksums, ipv4_packet_view& packet) {
  size_t offset = 0;
  if (!locate_ip(data, size, datalink, offset)) {
    return false;
  }
  const uint8_t* header = data + offset;
  const size_t header_size = (header[0] & 0x0fU) * 4U;
  const size_t total_size = read_be16(header + 2);
  const uint16_t field = read_be16(header + 6);
  if (header_size < 20 || total_size < header_size ||
      total_size > size - offset || header[9] != kTcpProtocol ||
      (field & 0x8000U) != 0) {
    return false;
  }
  // Reject every fragment, including a first fragment with a full TCP header.
  // DF is allowed; MF and the 13-bit fragment offset must both be zero.
  if ((field & 0x3fffU) != 0) {
    return false;
  }
  if (verify_checksums && !valid_checksum(checksum_sum(header, header_size))) {
    return false;
  }
  packet.src_ip = read_be32(header + 12);
  packet.dst_ip = read_be32(header + 16);
  packet.payload = {header + header_size, total_size - header_size};
  return true;
}

bool parse_tcp(const ipv4_packet_view& datagram, bool verify_checksums,
               tcp_packet_view& packet) {
  if (datagram.payload.size < 20) {
    return false;
  }
  const uint8_t* tcp = datagram.payload.data;
  const size_t header_size = (tcp[12] >> 4) * 4U;
  if (header_size < 20 || header_size > datagram.payload.size) {
    return false;
  }
  const uint8_t control = tcp[13] & (tcp_syn | tcp_fin | tcp_rst);
  if (control != 0 && control != tcp_syn && control != tcp_fin &&
      control != tcp_rst) {
    return false;
  }
  if (verify_checksums) {
    const uint32_t pseudo_sum =
        (datagram.src_ip >> 16) + (datagram.src_ip & 0xffffU) +
        (datagram.dst_ip >> 16) + (datagram.dst_ip & 0xffffU) + kTcpProtocol +
        datagram.payload.size;
    if (!valid_checksum(checksum_sum(tcp, datagram.payload.size, pseudo_sum))) {
      return false;
    }
  }
  packet.tuple = {{datagram.src_ip, read_be16(tcp)},
                  {datagram.dst_ip, read_be16(tcp + 2)}};
  packet.sequence = read_be32(tcp + 4);
  packet.acknowledgment = read_be32(tcp + 8);
  packet.flags = tcp[13];
  packet.payload = {tcp + header_size, datagram.payload.size - header_size};
  return true;
}

struct pcap_deleter {
  void operator()(pcap_t* handle) const { pcap_close(handle); }
};

}  // namespace

rx_pcap_tcp::rx_pcap_tcp() = default;
rx_pcap_tcp::~rx_pcap_tcp() { reset(); }

rx_pcap_tcp::call_guard::call_guard(bool& busy) : busy_(busy) {
  if (busy_) {
    throw std::logic_error("rx_pcap_tcp callback reentry");
  }
  busy_ = true;
}

rx_pcap_tcp::call_guard::~call_guard() { busy_ = false; }

void rx_pcap_tcp::reset() {
  if (pcap_ != nullptr) {
    pcap_close(pcap_);
    pcap_ = nullptr;
  }
  manager_.reset();
  pending_.reset();
  last_result_ = {};
  source_result_ = 0;
}

void rx_pcap_tcp::close() {
  const call_guard guard(busy_);
  reset();
}

bool rx_pcap_tcp::open() {
  const call_guard guard(busy_);
  reset();
  error_.clear();
  if (cfg_.file_path.empty()) {
    error_ = "Pcap file path must not be empty";
    return false;
  }
  char error[PCAP_ERRBUF_SIZE] = {};
  std::unique_ptr<pcap_t, pcap_deleter> handle(
      pcap_open_offline(cfg_.file_path.c_str(), error));
  if (!handle) {
    error_ = error;
    return false;
  }
  const int datalink = pcap_datalink(handle.get());
  if (!supported_datalink(datalink)) {
    error_ = "Unsupported pcap datalink type";
    return false;
  }
  try {
    auto manager = std::make_unique<tcp_stream_manager>(cfg_.streams);
    if (manager->replace_rules(cfg_.rules).status !=
        tcp_filter_update_status::updated) {
      error_ = "Duplicate TCP rule ID";
      return false;
    }
    active_cfg_ = cfg_;
    manager_ = std::move(manager);
  } catch (const std::invalid_argument& exception) {
    error_ = exception.what();
    return false;
  }
  datalink_ = datalink;
  pcap_ = handle.release();
  return true;
}

int rx_pcap_tcp::next_packet(tcp_packet_view& packet) {
  if (pcap_ == nullptr) {
    error_ = "TCP pcap receiver is not open";
    errno = EBADF;
    return -1;
  }
  if (source_result_ != 0) {
    error_ = source_result_ == -1 ? pcap_geterr(pcap_) : "";
    return source_result_;
  }
  error_.clear();
  for (;;) {
    pcap_pkthdr* header = nullptr;
    const uint8_t* bytes = nullptr;
    const int result = pcap_next_ex(pcap_, &header, &bytes);
    if (result == 0) {
      continue;
    }
    if (result < 0) {
      source_result_ = result;
      if (result == -1) {
        error_ = pcap_geterr(pcap_);
      }
      manager_->finish_all(result == -2 ? tcp_finish_reason::capture_end
                                        : tcp_finish_reason::capture_error);
      return result;
    }
    if (header == nullptr || bytes == nullptr || header->caplen > header->len ||
        header->ts.tv_sec < 0 || header->ts.tv_usec < 0 ||
        header->ts.tv_usec >= 1000000) {
      continue;
    }
    ipv4_packet_view datagram;
    if (!parse_ip(bytes, header->caplen, datalink_,
                  active_cfg_.verify_checksums, datagram) ||
        !parse_tcp(datagram, active_cfg_.verify_checksums, packet)) {
      continue;
    }
    packet.domain_id = active_cfg_.domain_id;
    packet.timestamp_us =
        (static_cast<uint64_t>(header->ts.tv_sec) * 1000000U) +
        header->ts.tv_usec;
    return 1;
  }
}

int rx_pcap_tcp::insert_result() {
  if (last_result_.status == tcp_manager_insert_status::connection_limit ||
      last_result_.status == tcp_manager_insert_status::stream_rejected) {
    error_ = "TCP stream capacity or sequence window rejected the packet";
    errno = ENOBUFS;
    return -1;
  }
  if (last_result_.status == tcp_manager_insert_status::invalid_input ||
      last_result_.status == tcp_manager_insert_status::source_finished) {
    error_ = "TCP stream manager rejected the packet";
    errno = EINVAL;
    return -1;
  }
  return 1;
}

int rx_pcap_tcp::read(char* data, int len) {
  tcp_stream_context context;
  return read_from(context, data, len);
}

int rx_pcap_tcp::read_from(tcp_stream_context& context, char* data, int len) {
  const call_guard guard(busy_);
  if (data == nullptr || len <= 0) {
    error_ = "TCP read requires a nonempty destination";
    errno = EINVAL;
    return -1;
  }
  error_.clear();
  int copied = 0;
  const auto on_data = [&](const tcp_stream_context& current,
                           const readable_buffers& buffers,
                           tcp_stream_status /*status*/) -> size_t {
    const size_t count = std::min(static_cast<size_t>(len), buffers.size());
    const size_t first = std::min(count, buffers.first.size);
    std::memcpy(data, buffers.first.data, first);
    if (count > first) {
      std::memcpy(data + first, buffers.second.data, count - first);
    }
    context = current;
    copied = static_cast<int>(count);
    pending_ = count < buffers.size()
                   ? std::optional<tcp_connection_handle>{current.handle}
                   : std::nullopt;
    return buffers.size() - count;
  };
  const auto on_state = [](const tcp_stream_context& /*current*/,
                           const tcp_managed_stream_state& /*state*/) {};
  if (pending_ && manager_) {
    const auto handle = *pending_;
    pending_.reset();
    manager_->process(handle, on_data, on_state);
    if (copied != 0) {
      return copied;
    }
  }
  for (;;) {
    tcp_packet_view packet;
    const int result = next_packet(packet);
    if (result != 1) {
      return result;
    }
    last_result_ = manager_->insert(packet, on_data, on_state);
    if (insert_result() == -1) {
      return -1;
    }
    if (copied != 0) {
      return copied;
    }
  }
}

}  // namespace net

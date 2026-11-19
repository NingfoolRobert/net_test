#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <sys/time.h>
#include "ip_fragment_assembly.h"

namespace net {

struct reassembled_ip_packet {
  uint32_t src_ip = 0;
  uint32_t dst_ip = 0;
  uint8_t  protocol = 0;
  timeval  timestamp{};
  const uint8_t *payload = nullptr;
  size_t payload_size = 0;
};

struct ip_fragment_key {
  uint32_t src_ip = 0;
  uint32_t dst_ip = 0;
  uint16_t identification = 0;
  uint8_t protocol = 0;
};

struct ipv4_fragment_view {
  ip_fragment_key key;
  size_t offset_bytes = 0;
  bool more_fragments = false;
  size_t ip_header_length = 20;
  const uint8_t *payload = nullptr;
  size_t payload_size = 0;
  timeval timestamp{};
};

enum class ip_reassembly_result {
  incomplete,
  complete,
  invalid_fragment,
  resource_limit,
};

class ip_reassembly {
  public:
  static constexpr size_t kMaxPacketSize = 65535;
  static constexpr size_t kMaxEntries = 64;

  ip_reassembly() = default;
  ip_reassembly(const ip_reassembly &) = delete;
  ip_reassembly &operator=(const ip_reassembly &) = delete;

  void configure(size_t max_packet_size, uint32_t timeout_ms,
                 size_t max_entries, size_t max_bytes);
  void clear();

  // The output points into a fixed internal buffer and is valid until that
  // slot is reused or clear() is called.
  ip_reassembly_result push(const ipv4_fragment_view &fragment,
                            reassembled_ip_packet &output);

  private:
  struct slot {
    bool active = false;
    ip_fragment_key key;
    timeval timestamp{};
    size_t max_payload_size = 0;
    ip_fragment_assembly assembly;
  };

  static uint64_t to_microseconds(const timeval &value);
  static bool same_key(const slot &value, const ip_fragment_key &key);
  void expire(const timeval &timestamp);
  void discard(slot &value);
  slot *find_slot(const ip_fragment_key &key);
  slot *allocate_slot(const ip_fragment_key &key, const timeval &timestamp);

  size_t max_packet_size_ = kMaxPacketSize;
  uint32_t timeout_ms_ = 3000;
  size_t max_entries_ = kMaxEntries;
  size_t max_bytes_ = kMaxEntries * kMaxPacketSize;
  size_t buffered_bytes_ = 0;
  std::array<slot, kMaxEntries> slots_{};
};

}  // namespace net

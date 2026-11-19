#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace net {

// Owns the payload buffer and coverage map for a single IPv4 datagram.
class ip_fragment_assembly {
 public:
  static constexpr size_t kMaxPayloadSize = 65535 - 20;
  enum class result { incomplete, complete, invalid };

  void reset();
  result insert(size_t offset, bool more_fragments, const uint8_t *payload,
                size_t payload_size, size_t max_payload_size);

  size_t buffered_size() const { return buffered_size_; }
  size_t max_received_end() const { return max_received_end_; }
  size_t total_payload_size() const { return total_payload_size_; }
  const uint8_t *payload() const { return payload_.data(); }

 private:
  bool flag(size_t offset) const;
  void set_flag(size_t offset, size_t length);

  bool first_seen_ = false;
  bool last_seen_ = false;
  bool complete_ = false;
  size_t total_payload_size_ = 0;
  size_t buffered_size_ = 0;
  size_t max_received_end_ = 0;
  std::array<uint8_t, kMaxPayloadSize> payload_{};
  std::array<uint64_t, (kMaxPayloadSize + 63) / 64> flags_{};
};

}  // namespace net

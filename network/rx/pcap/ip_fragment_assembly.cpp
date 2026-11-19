#include "ip_fragment_assembly.h"

#include <cstring>

namespace net {

void ip_fragment_assembly::reset() {
  first_seen_         = false;
  last_seen_          = false;
  complete_           = false;
  total_payload_size_ = 0;
  buffered_size_      = 0;
  max_received_end_   = 0;
  flags_.fill(0);
}

bool ip_fragment_assembly::flag(size_t offset) const {
  return (flags_[offset / 64] & (uint64_t{1} << (offset % 64))) != 0;
}

void ip_fragment_assembly::set_flag(size_t offset, size_t length) {
  for (size_t i = offset; i < offset + length; ++i) {
    flags_[i / 64] |= uint64_t{1} << (i % 64);
  }
}

ip_fragment_assembly::result ip_fragment_assembly::insert(
    size_t offset, bool more_fragments, const uint8_t *payload, size_t payload_size, size_t max_payload_size) {
  if (complete_ || payload == nullptr || payload_size == 0 || max_payload_size > kMaxPayloadSize ||
      offset > max_payload_size || payload_size > max_payload_size - offset ||
      (more_fragments && payload_size % 8 != 0)) {
    return result::invalid;
  }

  const size_t end = offset + payload_size;
  if (last_seen_ && end > total_payload_size_) {
    return result::invalid;
  }
  if (!more_fragments) {
    if (last_seen_ && total_payload_size_ != end) {
      return result::invalid;
    }
    if (max_received_end_ > end) {
      return result::invalid;
    }
  }
  for (size_t i = offset; i < end; ++i) {
    if (flag(i))
      return result::invalid;
  }

  std::memcpy(payload_.data() + offset, payload, payload_size);
  set_flag(offset, payload_size);
  buffered_size_ += payload_size;
  if (end > max_received_end_) {
    max_received_end_ = end;
  }
  if (offset == 0) {
    first_seen_ = true;
  }
  if (!more_fragments) {
    last_seen_          = true;
    total_payload_size_ = end;
  }

  if (!first_seen_ || !last_seen_) {
    return result::incomplete;
  }
  if (buffered_size_ != total_payload_size_) {
    return result::incomplete;
  }
  complete_ = true;
  return result::complete;
}

}  // namespace net

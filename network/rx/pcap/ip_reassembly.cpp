#include "ip_reassembly.h"

#include <algorithm>

namespace net {
namespace {
uint64_t micros(const timeval &value) {
  return static_cast<uint64_t>(value.tv_sec) * 1000000ULL + value.tv_usec;
}
}

uint64_t ip_reassembly::to_microseconds(const timeval &value) { return micros(value); }

bool ip_reassembly::same_key(const slot &value, const ip_fragment_key &key) {
  return value.active && value.key.src_ip == key.src_ip &&
         value.key.dst_ip == key.dst_ip &&
         value.key.identification == key.identification &&
         value.key.protocol == key.protocol;
}

void ip_reassembly::configure(size_t max_packet_size, uint32_t timeout_ms,
                              size_t max_entries, size_t max_bytes) {
  clear();
  max_packet_size_ = std::min(max_packet_size, kMaxPacketSize);
  max_entries_ = std::min(max_entries, kMaxEntries);
  max_bytes_ = std::min(max_bytes, kMaxEntries * kMaxPacketSize);
  timeout_ms_ = timeout_ms;
}

void ip_reassembly::clear() {
  for (auto &value : slots_) {
    value.active = false;
    value.assembly.reset();
  }
  buffered_bytes_ = 0;
}

void ip_reassembly::discard(slot &value) {
  buffered_bytes_ -= value.assembly.buffered_size();
  value.active = false;
  value.assembly.reset();
}

void ip_reassembly::expire(const timeval &timestamp) {
  const uint64_t now = to_microseconds(timestamp);
  const uint64_t timeout = static_cast<uint64_t>(timeout_ms_) * 1000ULL;
  for (auto &value : slots_) {
    if (value.active && now >= to_microseconds(value.timestamp) &&
        now - to_microseconds(value.timestamp) > timeout) {
      discard(value);
    }
  }
}

ip_reassembly::slot *ip_reassembly::find_slot(const ip_fragment_key &key) {
  for (auto &value : slots_) {
    if (same_key(value, key)) return &value;
  }
  return nullptr;
}

ip_reassembly::slot *ip_reassembly::allocate_slot(const ip_fragment_key &key,
                                                   const timeval &timestamp) {
  slot *value = nullptr;
  slot *oldest = nullptr;
  for (size_t index = 0; index < max_entries_; ++index) {
    if (!slots_[index].active) {
      value = &slots_[index];
      break;
    }
    if (oldest == nullptr || micros(slots_[index].timestamp) < micros(oldest->timestamp)) {
      oldest = &slots_[index];
    }
  }
  if (value == nullptr) value = oldest;
  if (value == nullptr) return nullptr;
  if (value->active) discard(*value);
  value->active = true;
  value->key = key;
  value->timestamp = timestamp;
  value->max_payload_size = max_packet_size_ >= 20 ? max_packet_size_ - 20 : 0;
  value->assembly.reset();
  return value;
}

ip_reassembly_result ip_reassembly::push(const ipv4_fragment_view &fragment,
                                         reassembled_ip_packet &output) {
  if (fragment.payload == nullptr || fragment.payload_size == 0 ||
      fragment.ip_header_length < 20 || fragment.ip_header_length > 60 ||
      fragment.ip_header_length % 4 != 0 ||
      max_packet_size_ < fragment.ip_header_length || max_packet_size_ < 20) {
    return ip_reassembly_result::invalid_fragment;
  }

  const size_t base_max_payload = max_packet_size_ - 20;
  if (fragment.offset_bytes > base_max_payload ||
      fragment.payload_size > base_max_payload - fragment.offset_bytes ||
      (fragment.more_fragments && fragment.payload_size % 8U != 0)) {
    return ip_reassembly_result::invalid_fragment;
  }

  expire(fragment.timestamp);
  slot *value = find_slot(fragment.key);
  if (value == nullptr) {
    if (fragment.payload_size > max_bytes_ - buffered_bytes_) {
      return ip_reassembly_result::resource_limit;
    }
    value = allocate_slot(fragment.key, fragment.timestamp);
    if (value == nullptr) return ip_reassembly_result::resource_limit;
  }

  size_t packet_payload_limit = value->max_payload_size;
  if (fragment.offset_bytes == 0) {
    packet_payload_limit = max_packet_size_ - fragment.ip_header_length;
    if (value->assembly.max_received_end() > packet_payload_limit) {
      discard(*value);
      return ip_reassembly_result::invalid_fragment;
    }
  }
  if (fragment.offset_bytes > packet_payload_limit ||
      fragment.payload_size > packet_payload_limit - fragment.offset_bytes) {
    discard(*value);
    return ip_reassembly_result::invalid_fragment;
  }
  if (fragment.payload_size > max_bytes_ - buffered_bytes_) {
    return ip_reassembly_result::resource_limit;
  }

  value->max_payload_size = packet_payload_limit;
  const auto assembly_result = value->assembly.insert(
      fragment.offset_bytes, fragment.more_fragments, fragment.payload,
      fragment.payload_size, packet_payload_limit);
  if (assembly_result == ip_fragment_assembly::result::invalid) {
    discard(*value);
    return ip_reassembly_result::invalid_fragment;
  }
  buffered_bytes_ += fragment.payload_size;
  if (assembly_result == ip_fragment_assembly::result::incomplete) {
    return ip_reassembly_result::incomplete;
  }
  output.src_ip = value->key.src_ip;
  output.dst_ip = value->key.dst_ip;
  output.protocol = value->key.protocol;
  output.timestamp = value->timestamp;
  output.payload = value->assembly.payload();
  output.payload_size = value->assembly.total_payload_size();
  value->active = false;
  buffered_bytes_ -= value->assembly.buffered_size();
  return ip_reassembly_result::complete;
}
}  // namespace net

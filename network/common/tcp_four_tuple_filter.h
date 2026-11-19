#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace net {

// IPv4 addresses and ports are in host byte order.
struct tcp_endpoint {
  uint32_t ip = 0;
  uint16_t port = 0;
  bool operator==(const tcp_endpoint& other) const;
  bool operator<(const tcp_endpoint& other) const;
};

struct tcp_four_tuple {
  tcp_endpoint src;
  tcp_endpoint dst;
  bool operator==(const tcp_four_tuple& other) const;
};

struct tcp_match_rule {
  uint64_t rule_id = 0;
  tcp_four_tuple tuple;
};

enum class tcp_filter_update_status : uint8_t { updated, duplicate_rule_id };
struct tcp_filter_update_result {
  tcp_filter_update_status status = tcp_filter_update_status::updated;
  uint64_t generation = 0;
  size_t terminated_connections = 0;
  // Buffered bytes discarded by manager-side rule revocation.
  size_t discarded_bytes = 0;
};

// Strict directional allowlist: all four fields must match exactly.
// Empty rules deny all. Zero addresses/ports are exact values, never wildcards.
class tcp_four_tuple_filter {
 public:
  tcp_four_tuple_filter();
  ~tcp_four_tuple_filter();
  tcp_four_tuple_filter(const tcp_four_tuple_filter&) = delete;
  tcp_four_tuple_filter& operator=(const tcp_four_tuple_filter&) = delete;
  tcp_four_tuple_filter(tcp_four_tuple_filter&&) = delete;
  tcp_four_tuple_filter& operator=(tcp_four_tuple_filter&&) = delete;

  // Builds off the packet path. Rejection/exception preserves the old rules.
  tcp_filter_update_result replace(const std::vector<tcp_match_rule>& rules);
  [[nodiscard]] bool matches(const tcp_four_tuple& tuple) const;
  [[nodiscard]] uint64_t generation() const { return generation_; }

 private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
  uint64_t generation_ = 0;
};

}  // namespace net

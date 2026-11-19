#include "tcp_four_tuple_filter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "robin_hood.h"

namespace net {
namespace {

struct tuple_hash {
  size_t operator()(const tcp_four_tuple& tuple) const {
    // Hash explicit fields, never struct padding.
    const uint64_t ips = (uint64_t{tuple.src.ip} << 32) | tuple.dst.ip;
    const uint64_t ports = (uint64_t{tuple.src.port} << 16) | tuple.dst.port;
    return robin_hood::hash<uint64_t>{}(ips) ^
           robin_hood::hash<uint64_t>{}(ports);
  }
};

}  // namespace

bool tcp_endpoint::operator==(const tcp_endpoint& other) const {
  return ip == other.ip && port == other.port;
}
bool tcp_endpoint::operator<(const tcp_endpoint& other) const {
  return std::tie(ip, port) < std::tie(other.ip, other.port);
}
bool tcp_four_tuple::operator==(const tcp_four_tuple& other) const {
  return src == other.src && dst == other.dst;
}

struct tcp_four_tuple_filter::implementation {
  robin_hood::unordered_flat_map<tcp_four_tuple, bool, tuple_hash> tuples;
};

tcp_four_tuple_filter::tcp_four_tuple_filter()
    : impl_(std::make_unique<implementation>()) {}
tcp_four_tuple_filter::~tcp_four_tuple_filter() = default;

tcp_filter_update_result tcp_four_tuple_filter::replace(
    const std::vector<tcp_match_rule>& rules) {
  std::vector<uint64_t> ids;
  ids.reserve(rules.size());
  for (const auto& rule : rules) {
    ids.push_back(rule.rule_id);
  }
  std::sort(ids.begin(), ids.end());
  if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
    return {tcp_filter_update_status::duplicate_rule_id, generation_, 0};
  }
  if (generation_ == std::numeric_limits<uint64_t>::max()) {
    throw std::overflow_error("TCP filter generation exhausted");
  }
  auto next = std::make_unique<implementation>();
  next->tuples.reserve(rules.size());
  for (const auto& rule : rules) {
    next->tuples.try_emplace(rule.tuple, true);
  }
  impl_.swap(next);
  ++generation_;
  return {tcp_filter_update_status::updated, generation_, 0};
}

bool tcp_four_tuple_filter::matches(const tcp_four_tuple& tuple) const {
  return impl_->tuples.contains(tuple);
}

}  // namespace net

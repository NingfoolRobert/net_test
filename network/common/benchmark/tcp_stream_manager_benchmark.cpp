#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <ratio>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "tcp_four_tuple_filter.h"
#include "tcp_stream_manager.h"
#include "tcp_stream_reassembly.h"

namespace {

using clock_type = std::chrono::steady_clock;

double elapsed_ns(clock_type::time_point start) {
  return std::chrono::duration<double, std::nano>(clock_type::now() - start)
      .count();
}
void report(std::string_view name, std::vector<double> samples) {
  double total = 0;
  for (const double value : samples) {
    total += value;
  }
  std::sort(samples.begin(), samples.end());
  std::cout << name << ": mean=" << total / static_cast<double>(samples.size())
            << " ns/op, p50=" << samples.at(samples.size() / 2)
            << ", p99=" << samples.at((samples.size() * 99) / 100) << '\n';
}
const auto ignore_state = [](const net::tcp_stream_context&,
                             const net::tcp_managed_stream_state&) {};
const auto consume_data = [](const net::tcp_stream_context&,
                             const net::readable_buffers&,
                             net::tcp_stream_status) { return size_t{0}; };

net::tcp_packet_view packet() {
  net::tcp_packet_view value;
  value.tuple = {{1, 100}, {2, 200}};
  value.sequence = 100;
  value.flags = net::tcp_ack;
  return value;
}
void allow_forward(net::tcp_stream_manager& manager) {
  manager.replace_rules({{1, {{1, 100}, {2, 200}}}});
}
void require_handled(const net::tcp_manager_insert_result& result) {
  if (result.status != net::tcp_manager_insert_status::handled) {
    throw std::runtime_error("Benchmark insertion rejected");
  }
}

void receive(size_t batch_size) {
  net::tcp_stream_manager manager({1, 32768});
  allow_forward(manager);
  std::array<uint8_t, 256> bytes{};
  auto incoming = packet();
  incoming.payload = {bytes.data(), bytes.size()};
  std::vector<double> samples;
  samples.reserve(10000);
  size_t consumed = 0;
  for (size_t sample = 0; sample != 11000; ++sample) {
    const auto start = clock_type::now();
    net::tcp_manager_insert_result inserted;
    for (size_t index = 0; index != batch_size; ++index) {
      inserted = batch_size == 1
                     ? manager.insert(incoming, consume_data, ignore_state)
                     : manager.insert(incoming);
      require_handled(inserted);
      incoming.sequence += static_cast<uint32_t>(bytes.size());
    }
    consumed +=
        batch_size == 1
            ? inserted.processing.consumed_bytes
            : manager.process(*inserted.handle, consume_data, ignore_state)
                  .stream.consumed_bytes;
    const double ns_per_packet =
        elapsed_ns(start) / static_cast<double>(batch_size);
    if (sample >= 1000) {
      samples.push_back(ns_per_packet);
    }
  }
  if (consumed != 11000 * batch_size * bytes.size()) {
    throw std::runtime_error("Lost benchmark data");
  }
  report(batch_size == 1 ? "immediate insert(callbacks) batch=1"
                         : "ordered insert+process batch=64 (amortized)",
         samples);
}

void reorder() {
  net::tcp_stream_manager manager({1, 4096});
  allow_forward(manager);
  auto incoming = packet();
  incoming.flags = net::tcp_syn;
  incoming.sequence = 99;
  const auto anchor = manager.insert(incoming);
  require_handled(anchor);
  manager.process(*anchor.handle, consume_data, ignore_state);
  incoming.flags = net::tcp_ack;
  std::array<uint8_t, 256> bytes{};
  incoming.payload = {bytes.data(), bytes.size()};
  uint32_t sequence = 100;
  std::vector<double> samples;
  samples.reserve(10000);
  for (size_t sample = 0; sample != 10000; ++sample) {
    const auto start = clock_type::now();
    incoming.sequence = sequence + 256;
    require_handled(manager.insert(incoming));
    incoming.sequence = sequence;
    require_handled(manager.insert(incoming));
    require_handled(manager.insert(incoming));
    incoming.sequence = sequence + 128;
    require_handled(manager.insert(incoming));
    const auto result =
        manager.process(*anchor.handle, consume_data, ignore_state).stream;
    samples.push_back(elapsed_ns(start) / 4);
    if (result.consumed_bytes != 512) {
      throw std::runtime_error("Invalid reordered delivery");
    }
    sequence += 512;
  }
  report("reorder+duplicate+overlap insert+process (4 packet average)",
         samples);
}

void filter_reject() {
  net::tcp_stream_manager manager({1, 4096});
  std::vector<net::tcp_match_rule> rules;
  for (uint64_t id = 1; id <= 4096; ++id) {
    rules.push_back(
        {id, {{static_cast<uint32_t>(1000 + id), 50000}, {90000, 60000}}});
  }
  manager.replace_rules(rules);
  std::array<uint8_t, 1> bytes{};
  auto incoming = packet();
  incoming.payload = {bytes.data(), bytes.size()};
  std::vector<double> samples;
  samples.reserve(10000);
  for (size_t sample = 0; sample != 10000; ++sample) {
    const auto start = clock_type::now();
    for (size_t index = 0; index != 64; ++index) {
      if (manager.insert(incoming).status !=
          net::tcp_manager_insert_status::filtered) {
        throw std::runtime_error("Unexpected filter admission");
      }
    }
    samples.push_back(elapsed_ns(start) / 64);
  }
  report("4096 strict four-tuple rules rejected insert (64 packet average)",
         samples);
}

void cold_slots() {
  net::tcp_stream_manager manager({4096, 4096});
  allow_forward(manager);
  auto incoming = packet();
  incoming.flags = net::tcp_syn;
  std::vector<double> samples;
  samples.reserve(4096);
  for (uint32_t domain = 0; domain != 4096; ++domain) {
    incoming.domain_id = domain;
    const auto start = clock_type::now();
    require_handled(manager.insert(incoming));
    samples.push_back(elapsed_ns(start));
  }
  report("first slot use SYN insert (two aligned allocations)", samples);
}

void warm_reuse() {
  net::tcp_stream_manager manager({1, 4096});
  allow_forward(manager);
  auto incoming = packet();
  std::vector<double> samples;
  samples.reserve(10000);
  for (size_t sample = 0; sample != 11000; ++sample) {
    const auto start = clock_type::now();
    incoming.flags = net::tcp_syn;
    const auto inserted = manager.insert(incoming);
    require_handled(inserted);
    incoming.flags = net::tcp_rst;
    require_handled(manager.insert(incoming));
    manager.process(*inserted.handle, consume_data, ignore_state);
    const auto released = manager.release(*inserted.handle);
    const double duration = elapsed_ns(start);
    if (released.status != net::tcp_release_status::released) {
      throw std::runtime_error("Benchmark release failed");
    }
    if (sample >= 1000) {
      samples.push_back(duration);
    }
  }
  report("warm slot entire SYN/RST/process/release lifecycle", samples);
}

}  // namespace

int main() {
  std::cout << std::fixed << std::setprecision(1);
  receive(1);
  receive(64);
  reorder();
  filter_reject();
  cold_slots();
  warm_reuse();
}

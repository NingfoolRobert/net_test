#include "tcp_stream_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "robin_hood.h"
#include "tcp_four_tuple_filter.h"
#include "tcp_stream_reassembly.h"

namespace net {
namespace {

constexpr size_t kNone = std::numeric_limits<size_t>::max();
constexpr uint32_t kHalfSequenceSpace = uint32_t{1} << 31;

bool has_flag(const tcp_packet_view& packet, uint8_t flag) {
  return (packet.flags & flag) != 0;
}
bool at_or_after(uint32_t sequence, uint32_t reference) {
  return sequence - reference < kHalfSequenceSpace;
}

struct connection_key {
  uint32_t domain = 0;
  tcp_four_tuple tuple;
  bool operator==(const connection_key& other) const {
    return domain == other.domain && tuple == other.tuple;
  }
};
struct connection_hash {
  size_t operator()(const connection_key& key) const {
    return robin_hood::hash<uint64_t>{}((uint64_t{key.tuple.src.ip} << 32) |
                                        key.tuple.dst.ip) ^
           robin_hood::hash<uint64_t>{}((uint64_t{key.tuple.src.port} << 48) |
                                        (uint64_t{key.tuple.dst.port} << 32) |
                                        key.domain);
  }
};

class operation_guard {
 public:
  explicit operation_guard(bool& busy) : busy_(busy) {
    if (busy_) {
      throw std::logic_error("tcp_stream_manager callback reentry");
    }
    busy_ = true;
  }
  ~operation_guard() { busy_ = false; }
  operation_guard(const operation_guard&) = delete;
  operation_guard& operator=(const operation_guard&) = delete;
  operation_guard(operation_guard&&) = delete;
  operation_guard& operator=(operation_guard&&) = delete;

 private:
  bool& busy_;
};

tcp_stream_manager_config checked_config(tcp_stream_manager_config config) {
  if (config.max_connections == 0 ||
      config.max_connections > std::numeric_limits<uint32_t>::max() ||
      config.stream_capacity < 64 ||
      config.stream_capacity >= kHalfSequenceSpace ||
      (config.stream_capacity & (config.stream_capacity - 1)) != 0) {
    throw std::invalid_argument("Invalid TCP stream manager capacity");
  }
  return config;
}

bool valid_packet(const tcp_packet_view& packet) {
  return (packet.payload.size == 0 || packet.payload.data != nullptr) &&
         packet.payload.size < kHalfSequenceSpace &&
         !(has_flag(packet, tcp_syn) &&
           (has_flag(packet, tcp_fin) || has_flag(packet, tcp_rst))) &&
         !(has_flag(packet, tcp_fin) && has_flag(packet, tcp_rst));
}

}  // namespace

struct tcp_stream_manager::implementation {
  struct stream_entry {
    explicit stream_entry(size_t capacity) : stream(capacity) {}
    void clear() {
      stream.clear();
      metadata = {};
    }
    tcp_stream_reassembly stream;
    connection_key key;
    tcp_stream_context context;
    tcp_managed_stream_state metadata;
  };
  struct slot {
    std::unique_ptr<stream_entry> entry;
    uint64_t generation = 1;
    bool active = false;
    size_t free_next = kNone;
  };
  explicit implementation(tcp_stream_manager_config options)
      : config(checked_config(options)), slots(config.max_connections) {
    connections.reserve(config.max_connections);
    for (size_t index = 0; index + 1 < slots.size(); ++index) {
      slots.at(index).free_next = index + 1;
    }
  }

  slot* find(tcp_connection_handle handle) {
    if (handle.slot >= slots.size()) {
      return nullptr;
    }
    auto& candidate = slots.at(handle.slot);
    return candidate.active && candidate.generation == handle.generation
               ? &candidate
               : nullptr;
  }
  [[nodiscard]] const slot* find(tcp_connection_handle handle) const {
    if (handle.slot >= slots.size()) {
      return nullptr;
    }
    const auto& candidate = slots.at(handle.slot);
    return candidate.active && candidate.generation == handle.generation
               ? &candidate
               : nullptr;
  }
  [[nodiscard]] tcp_connection_handle handle_at(size_t index) const {
    return {static_cast<uint32_t>(index), slots.at(index).generation};
  }
  void terminate(size_t index, tcp_finish_reason reason) {
    auto& entry = *slots.at(index).entry;
    entry.metadata.phase = tcp_connection_phase::terminated;
    entry.metadata.reason = reason;
  }

  tcp_connection_handle create(const connection_key& key,
                               const tcp_packet_view& packet) {
    const size_t index = free_head;
    auto& current = slots.at(index);
    if (!current.entry) {
      current.entry = std::make_unique<stream_entry>(config.stream_capacity);
      ++counters.allocated_slots;
    } else {
      current.entry->clear();
    }
    auto& entry = *current.entry;
    entry.key = key;
    entry.metadata.started_midstream = !has_flag(packet, tcp_syn);
    entry.metadata.first_seen_us = watermark_us;
    entry.metadata.last_seen_us = watermark_us;
    entry.metadata.filter_generation = filter.generation();
    const auto handle = handle_at(index);
    entry.context = {handle, key.domain, key.tuple};
    // Publish only after construction and hash insertion succeed.
    auto inserted = connections.try_emplace(key, handle);
    inserted.first->second = handle;
    free_head = current.free_next;
    current.free_next = kNone;
    current.active = true;
    ++counters.connections;

    return handle;
  }

  static bool needs_replacement(const stream_entry& entry,
                                const tcp_packet_view& packet) {
    if (!has_flag(packet, tcp_syn)) {
      return false;
    }
    return entry.metadata.syn_sequence
               ? *entry.metadata.syn_sequence != packet.sequence
               : entry.metadata.phase == tcp_connection_phase::terminated;
  }

  tcp_manager_insert_result admit(const connection_key& key,
                                  const tcp_packet_view& packet,
                                  tcp_manager_insert_result result) {
    if (!filter.matches(packet.tuple)) {
      ++counters.filtered_packets;
      result.status = tcp_manager_insert_status::filtered;
      return result;
    }
    if (has_flag(packet, tcp_rst) ||
        (!has_flag(packet, tcp_syn) &&
         (!config.allow_midstream || packet.payload.size == 0))) {
      result.status = tcp_manager_insert_status::not_creatable;
      return result;
    }
    if (packet.payload.size > config.stream_capacity) {
      result.status = tcp_manager_insert_status::stream_rejected;
      result.stream.status = tcp_insert_status::out_of_window;
      return result;
    }
    if (free_head == kNone) {
      ++counters.capacity_rejections;
      result.status = tcp_manager_insert_status::connection_limit;
      return result;
    }
    watermark_us = std::max(watermark_us, packet.timestamp_us);
    const auto old = result.handle;
    result.handle = create(key, packet);
    result.created = true;
    result.status = tcp_manager_insert_status::handled;
    if (old) {
      terminate(old->slot, tcp_finish_reason::replaced);
    }
    return result;
  }

  tcp_manager_insert_result resolve(const tcp_packet_view& packet) {
    const connection_key key{packet.domain_id, packet.tuple};
    tcp_manager_insert_result result;
    const auto existing = connections.find(key);
    if (existing == connections.end()) {
      return admit(key, packet, result);
    }
    result.handle = existing->second;
    const auto& entry = *slots.at(result.handle->slot).entry;
    if (entry.stream.is_processing()) {
      throw std::logic_error("Cannot insert into a stream inside its callback");
    }
    if (needs_replacement(entry, packet)) {
      return admit(key, packet, result);
    }
    result.status = tcp_manager_insert_status::handled;
    return result;
  }

  static tcp_manager_insert_status insert_stream(stream_entry& entry,
                                                 const tcp_packet_view& packet,
                                                 tcp_insert_result& result) {
    if (packet.payload.size == 0 && !has_flag(packet, tcp_syn) &&
        !has_flag(packet, tcp_fin)) {
      return tcp_manager_insert_status::handled;
    }
    const uint32_t sequence =
        packet.sequence + (has_flag(packet, tcp_syn) ? 1U : 0U);
    const uint32_t end = sequence + static_cast<uint32_t>(packet.payload.size);
    const tcp_stream_input input{{sequence, packet.payload},
                                 has_flag(packet, tcp_fin)
                                     ? std::optional<uint32_t>{end}
                                     : std::nullopt};
    result = entry.stream.insert(input);
    entry.metadata.last_insert_status = result.status;
    if (result.status != tcp_insert_status::accepted &&
        result.status != tcp_insert_status::duplicate) {
      ++entry.metadata.rejected_inserts;
      return tcp_manager_insert_status::stream_rejected;
    }
    if (has_flag(packet, tcp_syn)) {
      entry.metadata.syn_sequence = packet.sequence;
    }
    if (has_flag(packet, tcp_fin)) {
      entry.metadata.fin_sequence = end;
    }
    return tcp_manager_insert_status::handled;
  }

  tcp_manager_insert_result accept(const tcp_packet_view& packet,
                                   tcp_manager_insert_result result) {
    const size_t index = result.handle->slot;
    auto& entry = *slots.at(index).entry;
    // A FIN-ended direction can still fill capture gaps before its known end.
    if (entry.metadata.phase == tcp_connection_phase::terminated &&
        (entry.metadata.reason != tcp_finish_reason::fin_observed ||
         has_flag(packet, tcp_syn))) {
      result.status = tcp_manager_insert_status::connection_terminated;
      return result;
    }
    watermark_us = std::max(watermark_us, packet.timestamp_us);
    entry.metadata.last_seen_us = watermark_us;
    if (has_flag(packet, tcp_rst)) {
      terminate(index, tcp_finish_reason::rst_observed);
      return result;
    }
    result.status = insert_stream(entry, packet, result.stream);
    if (has_flag(packet, tcp_ack) &&
        (!entry.metadata.last_acknowledgment ||
         at_or_after(packet.acknowledgment,
                     *entry.metadata.last_acknowledgment))) {
      entry.metadata.last_acknowledgment = packet.acknowledgment;
    }
    if (result.status == tcp_manager_insert_status::handled &&
        has_flag(packet, tcp_fin)) {
      terminate(index, tcp_finish_reason::fin_observed);
    }

    return result;
  }

  [[nodiscard]] static tcp_managed_stream_state snapshot(
      const stream_entry& entry, const tcp_stream_state& stream) {
    auto state = entry.metadata;
    state.stream = stream;
    return state;
  }

  // Record identity and metadata live in the slot; processing only forwards
  // the core callbacks. There is no queue, scheduling or snapshot
  // reconstruction.
  static tcp_process_result forward(stream_entry& entry,
                                    callback_set callbacks) {
    return entry.stream.process(
        [&](const readable_buffers& buffers, tcp_stream_status status) {
          return callbacks.data(callbacks.opaque, entry.context, buffers,
                                status);
        },
        [&](const tcp_stream_state& stream) {
          entry.metadata.stream = stream;
          callbacks.state(callbacks.opaque, entry.context, entry.metadata);
        });
  }

  void ensure_no_processing() const {
    for (const auto& current : slots) {
      if (current.active && current.entry->stream.is_processing()) {
        throw std::logic_error(
            "Cannot change stream management inside a callback");
      }
    }
  }

  tcp_stream_manager_config config;
  tcp_four_tuple_filter filter;
  std::vector<slot> slots;
  robin_hood::unordered_flat_map<connection_key, tcp_connection_handle,
                                 connection_hash>
      connections;
  size_t free_head = 0;
  uint64_t watermark_us = 0;
  bool busy = false;
  tcp_manager_stats counters;
};

tcp_stream_manager::tcp_stream_manager(tcp_stream_manager_config config)
    : impl_(std::make_unique<implementation>(config)) {}
tcp_stream_manager::~tcp_stream_manager() = default;

tcp_manager_insert_result tcp_stream_manager::insert(
    const tcp_packet_view& packet) {
  return insert_impl(packet, nullptr);
}

tcp_manager_insert_result tcp_stream_manager::insert_impl(
    const tcp_packet_view& packet, const callback_set* callbacks) {
  auto& impl = *impl_;
  const operation_guard guard(impl.busy);
  tcp_manager_insert_result result;
  if (!valid_packet(packet)) {
    return result;
  }
  if (impl.counters.source_finished) {
    result.status = tcp_manager_insert_status::source_finished;
    return result;
  }
  result = impl.resolve(packet);
  if (result.status != tcp_manager_insert_status::handled) {
    return result;
  }
  result = impl.accept(packet, result);
  if (result.status == tcp_manager_insert_status::handled) {
    auto& entry = *impl.slots.at(result.handle->slot).entry;
    result.context = &entry.stream;
    if (callbacks != nullptr) {
      result.processing = implementation::forward(entry, *callbacks);
    }
  }
  return result;
}

tcp_filter_update_result tcp_stream_manager::replace_rules(
    const std::vector<tcp_match_rule>& rules) {
  auto& impl = *impl_;
  const operation_guard guard(impl.busy);
  impl.ensure_no_processing();
  auto result = impl.filter.replace(rules);
  if (result.status != tcp_filter_update_status::updated) {
    return result;
  }
  for (size_t index = 0; index != impl.slots.size(); ++index) {
    auto& current = impl.slots.at(index);
    if (!current.active) {
      continue;
    }
    auto& entry = *current.entry;
    entry.metadata.filter_generation = result.generation;
    if (!impl.filter.matches(entry.key.tuple)) {
      // Discard at revocation, so later direct core processing cannot expose
      // buffered bytes from a removed rule. Report the exact discarded count.
      const size_t discarded = entry.stream.buffered_size();
      entry.metadata.discarded_bytes += discarded;
      result.discarded_bytes += discarded;
      entry.stream.clear();
      impl.terminate(index, tcp_finish_reason::filter_removed);
      ++result.terminated_connections;
    }
  }
  return result;
}

tcp_manager_skip_result tcp_stream_manager::skip_gap(
    tcp_connection_handle handle) {
  auto& impl = *impl_;
  const operation_guard guard(impl.busy);
  auto* current = impl.find(handle);
  if (current == nullptr) {
    return {};
  }
  const auto skipped = current->entry->stream.skip_gap();
  return {true, skipped};
}

size_t tcp_stream_manager::finish_all(tcp_finish_reason reason) {
  auto& impl = *impl_;
  const operation_guard guard(impl.busy);
  if (reason != tcp_finish_reason::capture_end &&
      reason != tcp_finish_reason::capture_error &&
      reason != tcp_finish_reason::manual) {
    throw std::invalid_argument("Invalid source finish reason");
  }
  impl.ensure_no_processing();
  impl.counters.source_finished = true;
  size_t count = 0;
  for (size_t index = 0; index != impl.slots.size(); ++index) {
    const auto& current = impl.slots.at(index);
    if (current.active &&
        current.entry->metadata.phase == tcp_connection_phase::tracking) {
      impl.terminate(index, reason);
      ++count;
    }
  }
  return count;
}

tcp_release_result tcp_stream_manager::release(tcp_connection_handle handle,
                                               tcp_release_policy policy) {
  auto& impl = *impl_;
  const operation_guard guard(impl.busy);
  if (policy != tcp_release_policy::require_empty &&
      policy != tcp_release_policy::discard_buffered) {
    throw std::invalid_argument("Invalid TCP stream release policy");
  }
  auto* current = impl.find(handle);
  if (current == nullptr) {
    return {};
  }
  auto& entry = *current->entry;
  if (entry.stream.is_processing()) {
    throw std::logic_error("Cannot release a stream inside its callback");
  }
  if (entry.metadata.phase != tcp_connection_phase::terminated) {
    return {tcp_release_status::still_active, 0};
  }
  const size_t buffered = entry.stream.buffered_size();
  if (policy != tcp_release_policy::discard_buffered && buffered != 0) {
    return {tcp_release_status::buffered_data, 0};
  }
  const auto mapping = impl.connections.find(entry.key);
  if (mapping != impl.connections.end() && mapping->second == handle) {
    impl.connections.erase(entry.key);
  }
  entry.clear();
  current->active = false;
  --impl.counters.connections;
  // Retire instead of wrapping a generation and reviving an ancient handle.
  if (current->generation != std::numeric_limits<uint64_t>::max()) {
    ++current->generation;
    current->free_next = impl.free_head;
    impl.free_head = handle.slot;
  }
  return {tcp_release_status::released, buffered};
}

std::optional<tcp_managed_stream_state> tcp_stream_manager::state(
    tcp_connection_handle handle) const {
  const auto* current = std::as_const(*impl_).find(handle);
  if (current == nullptr) {
    return std::nullopt;
  }
  return implementation::snapshot(*current->entry,
                                  current->entry->stream.state());
}
tcp_manager_stats tcp_stream_manager::stats() const { return impl_->counters; }

tcp_manager_process_result tcp_stream_manager::process_one(
    tcp_connection_handle handle, callback_set callbacks) {
  const operation_guard guard(impl_->busy);
  auto* current = impl_->find(handle);
  if (current == nullptr) {
    return {};
  }
  return {tcp_manager_process_status::processed,
          implementation::forward(*current->entry, callbacks)};
}

}  // namespace net

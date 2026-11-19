#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "tcp_four_tuple_filter.h"
#include "tcp_stream_reassembly.h"

namespace net {

inline constexpr uint8_t tcp_fin = 0x01;
inline constexpr uint8_t tcp_syn = 0x02;
inline constexpr uint8_t tcp_rst = 0x04;
inline constexpr uint8_t tcp_ack = 0x10;

struct tcp_packet_view {
  uint32_t domain_id = 0;
  tcp_four_tuple tuple;
  uint32_t sequence = 0;
  uint32_t acknowledgment = 0;
  uint8_t flags = 0;
  byte_view payload;
  uint64_t timestamp_us = 0;
};

// Identifies a single directed record; opposite directions have separate
// handles.
struct tcp_connection_handle {
  uint32_t slot = 0;
  uint64_t generation = 0;  // Zero is never a valid handle.
  bool operator==(const tcp_connection_handle& other) const {
    return slot == other.slot && generation == other.generation;
  }
};

struct tcp_stream_context {
  tcp_connection_handle handle;
  uint32_t domain_id = 0;
  tcp_four_tuple tuple;
};

enum class tcp_connection_phase : uint8_t { tracking, terminated };
enum class tcp_finish_reason : uint8_t {
  none,
  fin_observed,
  rst_observed,
  filter_removed,
  capture_end,
  capture_error,
  replaced,
  manual
};

// A snapshot describes exactly one directed four-tuple, never its reverse.
struct tcp_managed_stream_state {
  tcp_stream_state stream;
  tcp_connection_phase phase = tcp_connection_phase::tracking;
  tcp_finish_reason reason = tcp_finish_reason::none;
  std::optional<uint32_t> syn_sequence;
  std::optional<uint32_t> fin_sequence;
  // ACK carried by this direction; only the caller can correlate its peer.
  std::optional<uint32_t> last_acknowledgment;
  bool started_midstream = false;
  uint64_t first_seen_us = 0;
  uint64_t last_seen_us = 0;
  uint64_t filter_generation = 0;
  uint64_t rejected_inserts = 0;
  uint64_t discarded_bytes = 0;
  tcp_insert_status last_insert_status = tcp_insert_status::accepted;
};

struct tcp_stream_manager_config {
  size_t max_connections =
      1024;  // Number of directed records, including ended ones.
  size_t stream_capacity = 65536;
  bool allow_midstream = true;
};

enum class tcp_manager_insert_status : uint8_t {
  handled,
  filtered,
  not_creatable,
  connection_limit,
  invalid_input,
  connection_terminated,
  source_finished,
  stream_rejected
};
struct tcp_manager_insert_result {
  tcp_manager_insert_status status = tcp_manager_insert_status::invalid_input;
  std::optional<tcp_connection_handle> handle;
  bool created = false;
  tcp_insert_result stream;
  // Borrowed until release; never use an old pointer after slot reuse.
  // Non-null only for handled inputs, including duplicates and control packets.
  // Only process/query through this pointer; manager owns insertion and
  // lifetime.
  tcp_stream_reassembly* context = nullptr;
  tcp_process_result processing;
};
enum class tcp_manager_process_status : uint8_t { processed, invalid_handle };
struct tcp_manager_process_result {
  tcp_manager_process_status status =
      tcp_manager_process_status::invalid_handle;
  tcp_process_result stream;
};
enum class tcp_release_policy : uint8_t { require_empty, discard_buffered };
enum class tcp_release_status : uint8_t {
  released,
  invalid_handle,
  still_active,
  buffered_data
};
struct tcp_release_result {
  tcp_release_status status = tcp_release_status::invalid_handle;
  size_t discarded_bytes = 0;
};
struct tcp_manager_skip_result {
  bool valid_handle = false;
  tcp_skip_result stream;
};
struct tcp_manager_stats {
  size_t connections = 0;
  size_t allocated_slots = 0;
  uint64_t filtered_packets = 0;
  uint64_t capacity_rejections = 0;
  bool source_finished = false;
};

// Single owner thread. Each slot owns ONE directed four-tuple and reassembler.
// No reverse lookup, peer state or implicit bidirectional management.
// Parsed IPv4/TCP input. No automatic scheduling or stored callbacks.
class tcp_stream_manager {
 public:
  explicit tcp_stream_manager(tcp_stream_manager_config config);
  ~tcp_stream_manager();
  tcp_stream_manager(const tcp_stream_manager&) = delete;
  tcp_stream_manager& operator=(const tcp_stream_manager&) = delete;
  tcp_stream_manager(tcp_stream_manager&&) = delete;
  tcp_stream_manager& operator=(tcp_stream_manager&&) = delete;

  // Copies only new payload bytes. Neither this nor the control APIs call
  // users.
  tcp_manager_insert_result insert(const tcp_packet_view& packet);

  // One lookup: insert into this flow, then immediately invoke its process().
  // Rejected inputs never call either callback. Callback signatures match the
  // manager's process() below; result.context also permits direct core access.
  // Borrow callbacks as lvalues throughout this synchronous operation.
  // NOLINTBEGIN(cppcoreguidelines-missing-std-forward)
  template <typename DataFn, typename StateFn>
  tcp_manager_insert_result insert(const tcp_packet_view& packet,
                                   DataFn&& on_data, StateFn&& on_state) {
    callback_adapter<DataFn, StateFn> adapter{std::addressof(on_data),
                                              std::addressof(on_state)};
    const auto callbacks = adapter.callbacks();
    return insert_impl(packet, &callbacks);
  }
  // NOLINTEND(cppcoreguidelines-missing-std-forward)
  tcp_filter_update_result replace_rules(
      const std::vector<tcp_match_rule>& rules);
  tcp_manager_skip_result skip_gap(tcp_connection_handle handle);
  // Stops future insertions; reasons: capture_end, capture_error, manual.
  size_t finish_all(tcp_finish_reason reason = tcp_finish_reason::capture_end);
  // Requires this directed stream to be terminated. Caller owns notification
  // handling, including when processing result.context directly.
  // Default retains buffered data; discard_buffered explicitly drops it.
  tcp_release_result release(
      tcp_connection_handle handle,
      tcp_release_policy policy = tcp_release_policy::require_empty);

  [[nodiscard]] std::optional<tcp_managed_stream_state> state(
      tcp_connection_handle handle) const;
  [[nodiscard]] tcp_manager_stats stats() const;

  // on_data(context, buffers, stream_status) returns the remaining suffix size.
  // on_state(context, snapshot) runs once on every valid process, even
  // unchanged. Callback references and buffers expire on return.
  // Mutation/reentry throws.
  // Callbacks remain lvalues so a mutable callback can be invoked repeatedly.
  // NOLINTBEGIN(cppcoreguidelines-missing-std-forward)
  template <typename DataFn, typename StateFn>
  tcp_manager_process_result process(tcp_connection_handle handle,
                                     DataFn&& on_data, StateFn&& on_state) {
    callback_adapter<DataFn, StateFn> adapter{std::addressof(on_data),
                                              std::addressof(on_state)};
    return process_one(handle, adapter.callbacks());
  }

  // NOLINTEND(cppcoreguidelines-missing-std-forward)

 private:
  struct callback_set {
    void* opaque;
    size_t (*data)(void*, const tcp_stream_context&, const readable_buffers&,
                   tcp_stream_status);
    void (*state)(void*, const tcp_stream_context&,
                  const tcp_managed_stream_state&);
  };
  // Stack-only type erasure; no std::function allocation or stored callbacks.
  template <typename DataFn, typename StateFn>
  struct callback_adapter {
    std::remove_reference_t<DataFn>* data;
    std::remove_reference_t<StateFn>* state;
    callback_set callbacks() {
      return {this,
              [](void* opaque, const tcp_stream_context& context,
                 const readable_buffers& buffers, tcp_stream_status status) {
                return static_cast<size_t>(
                    (*static_cast<callback_adapter*>(opaque)->data)(
                        context, buffers, status));
              },
              [](void* opaque, const tcp_stream_context& context,
                 const tcp_managed_stream_state& snapshot) {
                (*static_cast<callback_adapter*>(opaque)->state)(context,
                                                                 snapshot);
              }};
    }
  };
  tcp_manager_process_result process_one(tcp_connection_handle handle,
                                         callback_set callbacks);
  tcp_manager_insert_result insert_impl(const tcp_packet_view& packet,
                                        const callback_set* callbacks);
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

}  // namespace net

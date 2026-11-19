#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

namespace net {

struct byte_view {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

struct readable_buffers {
  byte_view first;
  byte_view second;

  [[nodiscard]] size_t size() const { return first.size + second.size; }
};

struct tcp_payload_view {
  uint32_t sequence = 0;  // Sequence of the first payload byte, excluding SYN.
  byte_view payload;
};

struct tcp_stream_input {
  tcp_payload_view payload;
  // Exclusive payload end, normally supplied by the connection's FIN parser.
  std::optional<uint32_t> end_sequence;
};

enum class tcp_insert_status : uint8_t {
  accepted,
  duplicate,
  invalid_input,
  out_of_window,
  invalid_end,
};

enum class tcp_callback_status : uint8_t {
  not_called,
  completed,
  invalid_remaining
};

struct tcp_process_result {
  tcp_callback_status status = tcp_callback_status::not_called;
  size_t offered_bytes = 0;
  size_t consumed_bytes = 0;
  size_t remaining_bytes = 0;
  bool state_notified = false;
};

struct tcp_insert_result {
  tcp_insert_status status = tcp_insert_status::accepted;
  size_t new_bytes = 0;
  // Includes consumed retransmissions and covered bytes, without comparing
  // data.
  size_t overlap_bytes = 0;
};

struct tcp_gap_info {
  uint32_t missing_begin = 0;
  uint32_t missing_end = 0;
  size_t missing_bytes = 0;
  size_t contiguous_after_gap_bytes = 0;
  size_t buffered_after_gap_bytes = 0;
  bool has_data_after_gap = false;

  bool operator==(const tcp_gap_info& other) const;
  bool operator!=(const tcp_gap_info& other) const { return !(*this == other); }
};

enum class tcp_stream_status : uint8_t {
  uninitialized = 0,
  waiting_data = 1,
  data_ready = 2,
  gap_waiting = 3,
  end_received = 4,
};

struct tcp_stream_state {
  tcp_stream_status status = tcp_stream_status::uninitialized;
  bool initialized = false;
  uint32_t next_sequence = 0;
  size_t readable_bytes = 0;
  size_t buffered_bytes = 0;
  std::optional<uint32_t> end_sequence;
  bool end_received =
      false;  // All bytes up to the end are received or skipped.
  bool had_skipped_gap = false;
  std::optional<tcp_gap_info> gap;

  bool operator==(const tcp_stream_state& other) const;
  bool operator!=(const tcp_stream_state& other) const {
    return !(*this == other);
  }
};

enum class tcp_skip_status : uint8_t { skipped, no_gap };

struct tcp_skip_result {
  tcp_skip_status status = tcp_skip_status::no_gap;
  size_t missing_bytes = 0;
  size_t discarded_ready_bytes = 0;
  uint32_t resume_sequence = 0;
};

// One owner thread, one connection direction. The first accepted insert selects
// the initial payload sequence, including an empty input supplied for a SYN.
// Owns two 64-byte-aligned POSIX allocations; never grows them after
// construction.
class tcp_stream_reassembly {
 public:
  // capacity must be a power of two in [64, 2^31).
  explicit tcp_stream_reassembly(size_t capacity);
  ~tcp_stream_reassembly();
  tcp_stream_reassembly(const tcp_stream_reassembly&) = delete;
  tcp_stream_reassembly& operator=(const tcp_stream_reassembly&) = delete;
  tcp_stream_reassembly(tcp_stream_reassembly&&) = delete;
  tcp_stream_reassembly& operator=(tcp_stream_reassembly&&) = delete;

  // Discards all data/history without callbacks, retaining allocated storage.
  // The next accepted insert selects a new initial sequence.
  void clear();

  // Only inserts/reassembles data and updates state; never invokes callbacks.
  // Rejected inputs leave state unchanged. Multiple inserts can be batched.
  tcp_insert_result insert(const tcp_stream_input& input);

  // on_data(view, status) returns the unconsumed suffix length in [0, size()].
  // Then on_state(snapshot) observes the latest state after consumption exactly
  // once per call, even without data or state changes. If on_data throws, this
  // operation exits without invoking on_state.
  // Intermediate batch states are coalesced, not queued. Callbacks cannot
  // mutate this instance; views/snapshot references expire on callback return.
  template <typename DataFn, typename StateFn>
  tcp_process_result process(DataFn&& on_data, StateFn&& on_state) {
    const operation_guard guard(processing_);
    tcp_process_result result;
    if (ready_end_ != head_position_) {
      const auto buffers = readable();
      result.offered_bytes = buffers.size();
      result.remaining_bytes = buffers.size();
      const size_t remaining = std::forward<DataFn>(on_data)(buffers, status());
      if (remaining > buffers.size()) {
        result.status = tcp_callback_status::invalid_remaining;
      } else {
        result.status = tcp_callback_status::completed;
        result.remaining_bytes = remaining;
        result.consumed_bytes = buffers.size() - remaining;
        consume(result.consumed_bytes);
      }
    }
    const auto snapshot = state();
    std::forward<StateFn>(on_state)(snapshot);
    result.state_notified = true;
    return result;
  }

  // Explicit discontinuity: drops the unconsumed prefix before the gap, keeps
  // later data, and updates state. Notifications occur only in process().
  tcp_skip_result skip_gap();

  [[nodiscard]] size_t capacity() const { return capacity_; }
  [[nodiscard]] bool is_processing() const { return processing_; }
  [[nodiscard]] size_t buffered_size() const { return buffered_bytes_; }
  [[nodiscard]] size_t readable_size() const {
    return static_cast<size_t>(ready_end_ - head_position_);
  }
  [[nodiscard]] tcp_stream_state state() const;
  [[nodiscard]] tcp_stream_status status() const;

 private:
  class operation_guard {
   public:
    explicit operation_guard(bool& processing) : processing_(processing) {
      if (processing_) {
        throw std::logic_error("tcp_stream_reassembly callback reentry");
      }
      processing_ = true;
    }
    ~operation_guard() { processing_ = false; }
    operation_guard(const operation_guard&) = delete;
    operation_guard& operator=(const operation_guard&) = delete;
    operation_guard(operation_guard&&) = delete;
    operation_guard& operator=(operation_guard&&) = delete;

   private:
    bool& processing_;
  };

  struct insertion_plan {
    tcp_insert_status status = tcp_insert_status::accepted;
    size_t source_offset = 0;
    uint64_t begin = 0;
    uint64_t end = 0;
    std::optional<uint64_t> stream_end;
  };

  [[nodiscard]] insertion_plan prepare_input(
      const tcp_stream_input& input) const;
  [[nodiscard]] readable_buffers readable() const;
  void consume(size_t size);
  void refresh_gap(uint64_t inserted_begin, uint64_t inserted_end);
  void copy_to_ring(uint64_t position, const uint8_t* data, size_t size);
  void mark_range(uint64_t position, size_t size, bool received);
  [[nodiscard]] size_t run_length(uint64_t position, size_t limit,
                                  bool received) const;
  [[nodiscard]] uint32_t sequence_at(uint64_t position) const;

  static constexpr size_t kAlignment = 64;
  const size_t capacity_;
  const size_t bitmap_word_count_;
  uint8_t* buffer_ = nullptr;
  uint64_t* bit_map_ = nullptr;

  bool initialized_ = false;
  bool processing_ = false;
  bool had_skipped_gap_ = false;
  uint32_t head_sequence_ = 0;
  // Positions increase independently of the wrapping 32-bit wire sequence.
  uint64_t head_position_ = 0;
  uint64_t ready_end_ = 0;
  uint64_t highest_end_ = 0;
  uint64_t first_pending_ = 0;
  uint64_t pending_run_end_ = 0;
  size_t buffered_bytes_ = 0;
  std::optional<uint64_t> end_position_;
  std::optional<tcp_gap_info> gap_;
};

}  // namespace net

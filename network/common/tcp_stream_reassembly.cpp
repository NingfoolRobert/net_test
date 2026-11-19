#include "tcp_stream_reassembly.h"

// POSIX declares posix_memalign here, outside the C++ standard library API.
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>

namespace net {
namespace {

constexpr uint64_t kSequenceSpace = uint64_t{1} << 32;
constexpr uint32_t kHalfSequenceSpace = uint32_t{1} << 31;
constexpr size_t kWordBits = 64;

size_t checked_capacity(size_t capacity) {
  if (capacity < kWordBits || capacity >= kHalfSequenceSpace ||
      (capacity & (capacity - 1)) != 0) {
    throw std::invalid_argument(
        "TCP stream capacity must be a power of two in [64, 2^31)");
  }
  return capacity;
}

// Avoid implementation-defined unsigned-to-signed narrowing at sequence wrap.
int64_t sequence_offset(uint32_t sequence, uint32_t head) {
  const uint32_t distance = sequence - head;
  return distance < kHalfSequenceSpace
             ? static_cast<int64_t>(distance)
             : static_cast<int64_t>(distance) -
                   static_cast<int64_t>(kSequenceSpace);
}

uint64_t low_mask(size_t bits) {
  return bits == kWordBits ? std::numeric_limits<uint64_t>::max()
                           : (uint64_t{1} << bits) - 1;
}

[[noreturn]] void allocation_error(int error) {
  if (error == ENOMEM) {
    throw std::bad_alloc();
  }
  throw std::system_error(error, std::generic_category(), "posix_memalign");
}

}  // namespace

bool tcp_gap_info::operator==(const tcp_gap_info& other) const {
  return missing_begin == other.missing_begin &&
         missing_end == other.missing_end &&
         missing_bytes == other.missing_bytes &&
         contiguous_after_gap_bytes == other.contiguous_after_gap_bytes &&
         buffered_after_gap_bytes == other.buffered_after_gap_bytes &&
         has_data_after_gap == other.has_data_after_gap;
}

bool tcp_stream_state::operator==(const tcp_stream_state& other) const {
  return status == other.status && initialized == other.initialized &&
         next_sequence == other.next_sequence &&
         readable_bytes == other.readable_bytes &&
         buffered_bytes == other.buffered_bytes &&
         end_sequence == other.end_sequence &&
         end_received == other.end_received &&
         had_skipped_gap == other.had_skipped_gap && gap == other.gap;
}

tcp_stream_reassembly::tcp_stream_reassembly(size_t capacity)
    : capacity_(checked_capacity(capacity)),
      bitmap_word_count_(capacity_ / kWordBits) {
  void* buffer_memory = nullptr;
  int error = posix_memalign(&buffer_memory, kAlignment, capacity_);
  if (error != 0) {
    allocation_error(error);
  }
  void* bitmap_memory = nullptr;
  error = posix_memalign(&bitmap_memory, kAlignment,
                         bitmap_word_count_ * sizeof(uint64_t));
  if (error != 0) {
    // Raw POSIX allocations are paired with free, including partial
    // construction.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    std::free(buffer_memory);
    allocation_error(error);
  }
  buffer_ = static_cast<uint8_t*>(buffer_memory);
  bit_map_ = static_cast<uint64_t*>(bitmap_memory);
  std::memset(bit_map_, 0, bitmap_word_count_ * sizeof(uint64_t));
}

tcp_stream_reassembly::~tcp_stream_reassembly() {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
  std::free(buffer_);
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
  std::free(bit_map_);
}

void tcp_stream_reassembly::clear() {
  const operation_guard guard(processing_);
  std::memset(bit_map_, 0, bitmap_word_count_ * sizeof(uint64_t));
  initialized_ = false;
  had_skipped_gap_ = false;
  head_sequence_ = 0;
  head_position_ = 0;
  ready_end_ = 0;
  highest_end_ = 0;
  first_pending_ = 0;
  pending_run_end_ = 0;
  buffered_bytes_ = 0;
  end_position_.reset();
  gap_.reset();
}

size_t tcp_stream_reassembly::run_length(uint64_t position, size_t limit,
                                         bool received) const {
  size_t length = 0;
  while (length < limit) {
    const size_t index = static_cast<size_t>(position) & (capacity_ - 1);
    const size_t shift = index % kWordBits;
    const size_t count = std::min(kWordBits - shift, limit - length);
    uint64_t word = bit_map_[index / kWordBits] >> shift;
    if (!received) {
      word = ~word;
    }
    const uint64_t mask = low_mask(count);
    if ((word & mask) != mask) {
      // This branch guarantees ~word != 0, as required by ctz.
      return length + static_cast<size_t>(__builtin_ctzll(~word));
    }
    length += count;
    position += count;
  }
  return length;
}

void tcp_stream_reassembly::mark_range(uint64_t position, size_t size,
                                       bool received) {
  while (size != 0) {
    const size_t index = static_cast<size_t>(position) & (capacity_ - 1);
    const size_t shift = index % kWordBits;
    const size_t count = std::min(kWordBits - shift, size);
    const uint64_t mask = low_mask(count) << shift;
    auto& word = bit_map_[index / kWordBits];
    if (received) {
      word |= mask;
    } else {
      word &= ~mask;
    }
    position += count;
    size -= count;
  }
}

void tcp_stream_reassembly::copy_to_ring(uint64_t position, const uint8_t* data,
                                         size_t size) {
  const size_t index = static_cast<size_t>(position) & (capacity_ - 1);
  const size_t first_size = std::min(size, capacity_ - index);
  std::memcpy(buffer_ + index, data, first_size);
  if (first_size != size) {
    std::memcpy(buffer_, data + first_size, size - first_size);
  }
  mark_range(position, size, true);
}

tcp_stream_reassembly::insertion_plan tcp_stream_reassembly::prepare_input(
    const tcp_stream_input& input) const {
  insertion_plan result;
  const uint32_t initial_sequence =
      initialized_ ? head_sequence_ : input.payload.sequence;
  const auto& payload = input.payload.payload;
  const uint32_t distance = input.payload.sequence - initial_sequence;
  if ((payload.size != 0 && payload.data == nullptr) ||
      payload.size >= kHalfSequenceSpace ||
      (payload.size != 0 && distance == kHalfSequenceSpace)) {
    result.status = tcp_insert_status::invalid_input;
    return result;
  }

  const int64_t offset =
      sequence_offset(input.payload.sequence, initial_sequence);
  const size_t old_bytes =
      offset < 0 ? std::min(payload.size, static_cast<size_t>(-offset)) : 0;
  const size_t remaining = payload.size - old_bytes;
  const size_t begin_offset = offset > 0 ? static_cast<size_t>(offset) : 0;
  if (remaining != 0 &&
      (begin_offset >= capacity_ || remaining > capacity_ - begin_offset)) {
    result.status = tcp_insert_status::out_of_window;
    return result;
  }

  const uint64_t begin =
      remaining == 0 ? head_position_ : head_position_ + begin_offset;
  const uint64_t end = begin + remaining;
  auto candidate_end = end_position_;
  if (input.end_sequence.has_value()) {
    const uint32_t end_distance = *input.end_sequence - initial_sequence;
    const int64_t end_offset =
        sequence_offset(*input.end_sequence, initial_sequence);
    if (end_distance == kHalfSequenceSpace || end_offset < 0) {
      result.status = tcp_insert_status::invalid_end;
      return result;
    }
    const uint64_t position =
        head_position_ + static_cast<uint64_t>(end_offset);
    if ((candidate_end.has_value() && *candidate_end != position) ||
        position < highest_end_) {
      result.status = tcp_insert_status::invalid_end;
      return result;
    }
    candidate_end = position;
  }
  if (candidate_end.has_value() && end > *candidate_end) {
    result.status = tcp_insert_status::invalid_end;
    return result;
  }

  result.source_offset = old_bytes;
  result.begin = begin;
  result.end = end;
  result.stream_end = candidate_end;
  return result;
}

tcp_insert_result tcp_stream_reassembly::insert(const tcp_stream_input& input) {
  const operation_guard guard(processing_);
  const auto plan = prepare_input(input);
  tcp_insert_result result;
  result.status = plan.status;
  if (result.status != tcp_insert_status::accepted) {
    return result;
  }
  // All validation precedes writes, including the optional stream end.
  if (!initialized_) {
    head_sequence_ = input.payload.sequence;
    initialized_ = true;
  }
  end_position_ = plan.stream_end;
  const auto& payload = input.payload.payload;
  const uint64_t begin = plan.begin;
  const uint64_t end = plan.end;
  const bool no_pending = buffered_bytes_ == readable_size();
  uint64_t position = begin;
  size_t source_offset = plan.source_offset;
  if (position < ready_end_) {
    const auto covered =
        static_cast<size_t>(std::min(end, ready_end_) - position);
    position += covered;
    source_offset += covered;
  }
  if (position < end && no_pending) {
    result.new_bytes = static_cast<size_t>(end - position);
    copy_to_ring(position, payload.data + source_offset, result.new_bytes);
  } else {
    while (position < end) {
      const size_t covered =
          run_length(position, static_cast<size_t>(end - position), true);
      position += covered;
      source_offset += covered;
      const size_t uncovered =
          run_length(position, static_cast<size_t>(end - position), false);
      if (uncovered != 0) {
        copy_to_ring(position, payload.data + source_offset, uncovered);
        result.new_bytes += uncovered;
        position += uncovered;
        source_offset += uncovered;
      }
    }
  }

  buffered_bytes_ += result.new_bytes;
  result.overlap_bytes = payload.size - result.new_bytes;
  if (payload.size != 0 && result.new_bytes == 0) {
    result.status = tcp_insert_status::duplicate;
  }
  highest_end_ = std::max(highest_end_, end);
  if (begin <= ready_end_ && end > ready_end_) {
    ready_end_ = end;
    ready_end_ += run_length(
        ready_end_, static_cast<size_t>(highest_end_ - ready_end_), true);
  }
  refresh_gap(begin, end);
  return result;
}

uint32_t tcp_stream_reassembly::sequence_at(uint64_t position) const {
  return head_sequence_ + static_cast<uint32_t>(position - head_position_);
}

void tcp_stream_reassembly::refresh_gap(uint64_t inserted_begin,
                                        uint64_t inserted_end) {
  if (buffered_bytes_ > readable_size()) {
    uint64_t first = first_pending_;
    if (first <= ready_end_) {
      first = highest_end_;
    }
    if (inserted_begin > ready_end_ && inserted_begin < inserted_end) {
      first = std::min(first, inserted_begin);
    }
    if (first == highest_end_) {
      first = ready_end_ +
              run_length(ready_end_,
                         static_cast<size_t>(highest_end_ - ready_end_), false);
    }
    // Retain the known run, so repeated updates behind a large hole do not
    // repeatedly scan the empty hole or the already-known received run.
    uint64_t run_end = first == first_pending_ ? pending_run_end_ : first;
    run_end = std::max(run_end, first);
    run_end +=
        run_length(run_end, static_cast<size_t>(highest_end_ - run_end), true);
    first_pending_ = first;
    pending_run_end_ = run_end;
    gap_ = tcp_gap_info{sequence_at(ready_end_),
                        sequence_at(first),
                        static_cast<size_t>(first - ready_end_),
                        static_cast<size_t>(run_end - first),
                        buffered_bytes_ - readable_size(),
                        true};
  } else {
    first_pending_ = ready_end_;
    pending_run_end_ = ready_end_;
    gap_.reset();
    if (end_position_.has_value() && ready_end_ < *end_position_) {
      gap_ = tcp_gap_info{sequence_at(ready_end_),
                          sequence_at(*end_position_),
                          static_cast<size_t>(*end_position_ - ready_end_),
                          0,
                          0,
                          false};
    }
  }
}

readable_buffers tcp_stream_reassembly::readable() const {
  const size_t index = static_cast<size_t>(head_position_) & (capacity_ - 1);
  const size_t first_size = std::min(readable_size(), capacity_ - index);
  return {{buffer_ + index, first_size},
          {buffer_, readable_size() - first_size}};
}

void tcp_stream_reassembly::consume(size_t size) {
  mark_range(head_position_, size, false);
  head_position_ += size;
  head_sequence_ += static_cast<uint32_t>(size);
  buffered_bytes_ -= size;
}

tcp_skip_result tcp_stream_reassembly::skip_gap() {
  const operation_guard guard(processing_);
  tcp_skip_result result;
  if (!gap_.has_value()) {
    return result;
  }
  result.status = tcp_skip_status::skipped;
  result.missing_bytes = gap_->missing_bytes;
  result.discarded_ready_bytes = readable_size();
  result.resume_sequence = gap_->missing_end;
  consume(result.discarded_ready_bytes);
  head_position_ += result.missing_bytes;
  head_sequence_ = result.resume_sequence;
  ready_end_ = head_position_;
  highest_end_ = std::max(highest_end_, head_position_);
  ready_end_ += run_length(
      ready_end_, static_cast<size_t>(highest_end_ - ready_end_), true);
  had_skipped_gap_ = true;
  refresh_gap(head_position_, head_position_);
  return result;
}

tcp_stream_state tcp_stream_reassembly::state() const {
  tcp_stream_state result;
  result.status = status();
  result.initialized = initialized_;
  result.next_sequence = head_sequence_;
  result.readable_bytes = readable_size();
  result.buffered_bytes = buffered_bytes_;
  if (end_position_.has_value()) {
    result.end_sequence = sequence_at(*end_position_);
    result.end_received = ready_end_ == *end_position_;
  }
  result.had_skipped_gap = had_skipped_gap_;
  result.gap = gap_;
  return result;
}

tcp_stream_status tcp_stream_reassembly::status() const {
  if (!initialized_) {
    return tcp_stream_status::uninitialized;
  }
  if (gap_.has_value()) {
    return tcp_stream_status::gap_waiting;
  }
  if (end_position_.has_value() && ready_end_ == *end_position_) {
    return tcp_stream_status::end_received;
  }
  return readable_size() != 0 ? tcp_stream_status::data_ready
                              : tcp_stream_status::waiting_data;
}

}  // namespace net

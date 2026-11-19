#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcp_stream_reassembly.h"

namespace {

using net::tcp_callback_status;
using net::tcp_gap_info;
using net::tcp_insert_status;
using net::tcp_skip_status;
using net::tcp_stream_reassembly;
using net::tcp_stream_state;

net::tcp_stream_input input(uint32_t sequence, std::string_view payload = {},
                            std::optional<uint32_t> end = std::nullopt) {
  // Byte views deliberately alias string storage without allocating/copying.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* bytes = reinterpret_cast<const uint8_t*>(payload.data());
  return {{sequence, {bytes, payload.size()}}, end};
}

std::string flatten(const net::readable_buffers& buffers) {
  std::string result;
  result.append(buffers.first.data, buffers.first.data + buffers.first.size);
  result.append(buffers.second.data, buffers.second.data + buffers.second.size);
  return result;
}

const auto keep_data = [](const net::readable_buffers& buffers,
                          net::tcp_stream_status) { return buffers.size(); };
const auto ignore_state = [](const tcp_stream_state&) {};
const auto unexpected_data = [](const net::readable_buffers& buffers,
                                net::tcp_stream_status) {
  ADD_FAILURE() << "Unexpected data callback";
  return buffers.size();
};

struct feed_result {
  tcp_insert_status status = tcp_insert_status::accepted;
  size_t new_bytes = 0;
  size_t overlap_bytes = 0;
  net::tcp_process_result delivery;
};

template <typename DataFn, typename StateFn>
feed_result feed(tcp_stream_reassembly& stream,
                 const net::tcp_stream_input& value, DataFn&& on_data,
                 StateFn&& on_state) {
  const auto inserted = stream.insert(value);
  net::tcp_process_result delivered;
  if (inserted.status == tcp_insert_status::accepted ||
      inserted.status == tcp_insert_status::duplicate) {
    delivered = stream.process(std::forward<DataFn>(on_data),
                               std::forward<StateFn>(on_state));
  }
  return {inserted.status, inserted.new_bytes, inserted.overlap_bytes,
          delivered};
}

// An empty normalized SYN input establishes a known start before out-of-order
// data.
void anchor(tcp_stream_reassembly& stream, uint32_t sequence) {
  feed(stream, input(sequence), unexpected_data, ignore_state);
}

// GoogleTest assertions expand into branches. Keep scenarios and the
// independent byte-wise reference model explicit rather than splitting
// assertions to hide it. NOLINTBEGIN(readability-function-cognitive-complexity)
TEST(TcpStreamReassembly, ValidatesCapacityAndAlignsOwnedData) {
  for (const size_t capacity : {size_t{0}, size_t{1}, size_t{63}, size_t{65},
                                size_t{96}, size_t{1} << 31}) {
    EXPECT_THROW(tcp_stream_reassembly stream(capacity), std::invalid_argument);
  }
  tcp_stream_reassembly stream(64);
  EXPECT_EQ(stream.capacity(), 64U);
  anchor(stream, 100);
  feed(
      stream, input(100, "x"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        EXPECT_EQ(reinterpret_cast<uintptr_t>(buffers.first.data) % 64, 0U);
        return size_t{0};
      },
      ignore_state);
}

TEST(TcpStreamReassembly, AutomaticallyInitializesOnlyAfterAValidInsert) {
  tcp_stream_reassembly stream(64);
  EXPECT_FALSE(stream.state().initialized);
  EXPECT_EQ(stream.process(unexpected_data, ignore_state).status,
            tcp_callback_status::not_called);
  EXPECT_EQ(stream.skip_gap().status, tcp_skip_status::no_gap);
  const net::tcp_stream_input bad = {{100, {nullptr, 1}}, std::nullopt};
  EXPECT_EQ(feed(stream, bad, unexpected_data, ignore_state).status,
            tcp_insert_status::invalid_input);
  EXPECT_EQ(feed(stream, input(200, std::string(65, 'x')), unexpected_data,
                 ignore_state)
                .status,
            tcp_insert_status::out_of_window);
  EXPECT_EQ(feed(stream, input(200, "abc", 199), unexpected_data, ignore_state)
                .status,
            tcp_insert_status::invalid_end);
  EXPECT_FALSE(stream.state().initialized);
  EXPECT_EQ(stream.buffered_size(), 0U);
  feed(
      stream, input(500, "abc"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), "abc");
        return size_t{0};
      },
      ignore_state);
  EXPECT_TRUE(stream.state().initialized);
  EXPECT_EQ(stream.state().next_sequence, 503U);
  EXPECT_EQ(feed(stream, input(503U + (uint32_t{1} << 31), "x"),
                 unexpected_data, ignore_state)
                .status,
            tcp_insert_status::invalid_input);
}

TEST(TcpStreamReassembly,
     DeliversInOrderBinaryPayloadAndConsumedRetransmissions) {
  tcp_stream_reassembly stream(128);
  anchor(stream, 100);
  std::string output;
  auto consume = [&](const net::readable_buffers& buffers,
                     net::tcp_stream_status) {
    output += flatten(buffers);
    return size_t{0};
  };
  auto result =
      feed(stream, input(100, std::string("a\0b", 3)), consume, ignore_state);
  EXPECT_EQ(result.new_bytes, 3U);
  EXPECT_EQ(result.delivery.consumed_bytes, 3U);
  result = feed(stream, input(100, std::string("a\0bXYZ", 6)), consume,
                ignore_state);
  EXPECT_EQ(result.new_bytes, 3U);
  EXPECT_EQ(result.overlap_bytes, 3U);
  EXPECT_EQ(output, std::string("a\0bXYZ", 6));
  EXPECT_EQ(stream.state().next_sequence, 106U);
  EXPECT_EQ(
      feed(stream, input(100, "abc"), unexpected_data, ignore_state).status,
      tcp_insert_status::duplicate);
}

TEST(TcpStreamReassembly, PreservesFirstArrivalAcrossConflictingOverlap) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  feed(stream, input(104, "EFGH"), unexpected_data, ignore_state);
  feed(stream, input(106, "xxIJ"), unexpected_data, ignore_state);
  feed(stream, input(103, "dxxxxxxxK"), unexpected_data, ignore_state);
  std::string output;
  const auto result = feed(
      stream, input(100, "ABCzzzzzzzzzz"),
      [&](const net::readable_buffers& buffers, net::tcp_stream_status) {
        output = flatten(buffers);
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(output, "ABCdEFGHIJxKz");
  EXPECT_EQ(result.new_bytes, 4U);
  EXPECT_EQ(result.overlap_bytes, 9U);
}

TEST(TcpStreamReassembly, RetainsSuffixAndReoffersItWithNewData) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  const auto result = feed(
      stream, input(100, "abcdef"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), "abcdef");
        return size_t{2};
      },
      ignore_state);
  EXPECT_EQ(result.delivery.remaining_bytes, 2U);
  EXPECT_EQ(stream.state().next_sequence, 104U);
  feed(
      stream, input(106, "gh"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), "efgh");
        return size_t{4};
      },
      ignore_state);
  const auto drained = stream.process(
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), "efgh");
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(drained.consumed_bytes, 4U);
  EXPECT_EQ(stream.buffered_size(), 0U);
}

TEST(TcpStreamReassembly,
     StartsMidstreamWithoutSynAndDoesNotBackfillEarlierData) {
  tcp_stream_reassembly stream(128);
  std::string output;
  auto consume = [&](const net::readable_buffers& buffers,
                     net::tcp_stream_status) {
    output += flatten(buffers);
    return size_t{0};
  };
  feed(stream, input(5000, "tail"), consume, ignore_state);
  const auto earlier =
      feed(stream, input(4996, "HEADtail"), consume, ignore_state);
  EXPECT_EQ(earlier.status, tcp_insert_status::duplicate);
  EXPECT_EQ(earlier.overlap_bytes, 8U);
  feed(stream, input(5004, "next"), consume, ignore_state);
  EXPECT_EQ(output, "tailnext");
  EXPECT_EQ(stream.state().next_sequence, 5008U);
}

TEST(TcpStreamReassembly, WrapsRingWithoutMovingRetainedBytes) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  const uint8_t* retained = nullptr;
  feed(
      stream, input(100, std::string(60, 'A')),
      [&](const net::readable_buffers& buffers, net::tcp_stream_status) {
        retained = buffers.first.data + 40;
        return size_t{20};
      },
      ignore_state);
  feed(
      stream, input(160, std::string(20, 'B')),
      [&](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(buffers.first.data, retained);
        EXPECT_EQ(buffers.first.size, 24U);
        EXPECT_EQ(buffers.second.size, 16U);
        EXPECT_EQ(flatten(buffers),
                  std::string(20, 'A') + std::string(20, 'B'));
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(stream.state().next_sequence, 180U);
}

TEST(TcpStreamReassembly, ReportsGapProgressOnEveryProcessIncludingDuplicates) {
  tcp_stream_reassembly stream(1024);
  anchor(stream, 1000);
  std::vector<tcp_stream_state> events;
  auto notify = [&](const tcp_stream_state& event) { events.push_back(event); };
  feed(
      stream, input(1000, std::string(100, 'A')),
      [](const net::readable_buffers&, net::tcp_stream_status) {
        return size_t{0};
      },
      notify);
  events.clear();
  feed(stream, input(1200, std::string(200, 'B')), unexpected_data, notify);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(events.back().gap.has_value());
  EXPECT_EQ(events.back().gap->missing_begin, 1100U);
  EXPECT_EQ(events.back().gap->missing_end, 1200U);
  EXPECT_EQ(events.back().gap->missing_bytes, 100U);
  EXPECT_EQ(events.back().gap->contiguous_after_gap_bytes, 200U);
  feed(stream, input(1500, std::string(100, 'C')), unexpected_data, notify);
  EXPECT_EQ(events.back().gap->contiguous_after_gap_bytes, 200U);
  EXPECT_EQ(events.back().gap->buffered_after_gap_bytes, 300U);
  feed(stream, input(1500, std::string(100, 'D')), unexpected_data, notify);
  ASSERT_EQ(events.size(), 3U);
  EXPECT_EQ(events.at(1), events.at(2));
  feed(stream, input(1400, std::string(100, 'E')), unexpected_data, notify);
  EXPECT_EQ(events.back().gap->contiguous_after_gap_bytes, 400U);
  EXPECT_EQ(events.back().gap->buffered_after_gap_bytes, 400U);
  feed(
      stream, input(1100, std::string(100, 'F')),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(buffers.size(), 500U);
        return size_t{0};
      },
      notify);
  ASSERT_EQ(events.size(), 5U);
  EXPECT_FALSE(events.back().gap.has_value());
}

TEST(TcpStreamReassembly, DetectsAnEarlierIslandInsideTheCurrentGap) {
  tcp_stream_reassembly stream(256);
  anchor(stream, 100);
  feed(stream, input(200, "AAAA"), unexpected_data, ignore_state);
  feed(stream, input(150, "BB"), unexpected_data, ignore_state);
  ASSERT_TRUE(stream.state().gap.has_value());
  EXPECT_EQ(stream.state().gap->missing_end, 150U);
  EXPECT_EQ(stream.state().gap->contiguous_after_gap_bytes, 2U);
  feed(stream, input(100, std::string(50, 'C')), keep_data, ignore_state);
  EXPECT_EQ(stream.readable_size(), 52U);
  EXPECT_EQ(stream.state().gap->missing_begin, 152U);
  EXPECT_EQ(stream.state().gap->missing_end, 200U);
}

TEST(TcpStreamReassembly, SkipsGapAndReportsDiscardedUnconsumedPrefix) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  feed(stream, input(100, "abc"), keep_data, ignore_state);
  feed(stream, input(108, "ijk"), keep_data, ignore_state);
  const auto skipped = stream.skip_gap();
  EXPECT_EQ(skipped.status, tcp_skip_status::skipped);
  EXPECT_EQ(skipped.missing_bytes, 5U);
  EXPECT_EQ(skipped.discarded_ready_bytes, 3U);
  EXPECT_EQ(skipped.resume_sequence, 108U);
  EXPECT_TRUE(stream.state().had_skipped_gap);
  EXPECT_EQ(stream.buffered_size(), 3U);
  stream.process(
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), "ijk");
        return size_t{0};
      },
      [](const tcp_stream_state& event) {
        EXPECT_FALSE(event.gap.has_value());
        EXPECT_TRUE(event.had_skipped_gap);
      });
  EXPECT_EQ(feed(stream, input(103, "defghijk"), unexpected_data, ignore_state)
                .status,
            tcp_insert_status::duplicate);
  EXPECT_EQ(stream.skip_gap().status, tcp_skip_status::no_gap);
}

TEST(TcpStreamReassembly, AdvancesThroughMultipleGapsWithoutJoiningTheirData) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  feed(stream, input(105, "x"), unexpected_data, ignore_state);
  feed(stream, input(110, "y"), unexpected_data, ignore_state);
  stream.skip_gap();
  EXPECT_EQ(stream.state().gap->missing_begin, 106U);
  EXPECT_EQ(stream.state().gap->missing_end, 110U);
  std::string output;
  auto consume = [&](const net::readable_buffers& buffers,
                     net::tcp_stream_status) {
    output += flatten(buffers);
    return size_t{0};
  };
  stream.process(consume, ignore_state);
  EXPECT_EQ(output, "x");
  stream.skip_gap();
  stream.process(consume, ignore_state);
  EXPECT_EQ(output, "xy");
}

TEST(TcpStreamReassembly, SupportsSequenceWrapAndOldSegmentsAcrossZero) {
  tcp_stream_reassembly stream(128);
  constexpr uint32_t start = 0xfffffff0U;
  anchor(stream, start);
  feed(stream, input(4, "TAIL"), unexpected_data, ignore_state);
  feed(
      stream, input(start, std::string(20, 'X')),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), std::string(20, 'X') + "TAIL");
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(stream.state().next_sequence, 8U);
  EXPECT_EQ(feed(stream, input(0xfffffff8U, std::string(16, 'Y')),
                 unexpected_data, ignore_state)
                .status,
            tcp_insert_status::duplicate);
}

TEST(TcpStreamReassembly,
     RejectsWholeSegmentOutsideWindowAndCanRetryAfterDrain) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  feed(stream, input(100, std::string(64, 'A')), keep_data, ignore_state);
  const auto rejected =
      feed(stream, input(163, "BC", 165), unexpected_data, ignore_state);
  EXPECT_EQ(rejected.status, tcp_insert_status::out_of_window);
  EXPECT_EQ(stream.buffered_size(), 64U);
  EXPECT_FALSE(stream.state().end_sequence.has_value());
  stream.process([](const net::readable_buffers&,
                    net::tcp_stream_status) { return size_t{0}; },
                 ignore_state);
  const auto retried = feed(
      stream, input(163, "BC", 165),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), "C");
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(retried.new_bytes, 1U);
  EXPECT_EQ(retried.overlap_bytes, 1U);
  EXPECT_TRUE(stream.state().end_received);
}

TEST(TcpStreamReassembly, DetectsTailGapFromEndSequenceAndClearsItWhenFilled) {
  tcp_stream_reassembly stream(128);
  anchor(stream, 100);
  std::vector<tcp_stream_state> events;
  auto notify = [&](const tcp_stream_state& event) { events.push_back(event); };
  feed(stream, input(0, {}, 110), unexpected_data, notify);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.back().gap->missing_begin, 100U);
  EXPECT_EQ(events.back().gap->missing_end, 110U);
  EXPECT_FALSE(events.back().gap->has_data_after_gap);
  EXPECT_EQ(events.back().gap->contiguous_after_gap_bytes, 0U);
  feed(
      stream, input(100, "0123456789"),
      [](const net::readable_buffers&, net::tcp_stream_status) {
        return size_t{0};
      },
      notify);
  ASSERT_EQ(events.size(), 2U);
  EXPECT_FALSE(events.back().gap.has_value());
  EXPECT_TRUE(stream.state().end_received);
  EXPECT_EQ(stream.state().end_sequence, 110U);
  EXPECT_EQ(feed(stream, input(110, "x"), unexpected_data, notify).status,
            tcp_insert_status::invalid_end);
}

TEST(TcpStreamReassembly, SkipsKnownTailBeyondBufferWithoutInventingData) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  feed(stream, input(0, {}, 10000), unexpected_data, ignore_state);
  const auto skipped = stream.skip_gap();
  EXPECT_EQ(skipped.missing_bytes, 9900U);
  EXPECT_EQ(skipped.resume_sequence, 10000U);
  EXPECT_EQ(stream.buffered_size(), 0U);
  EXPECT_EQ(stream.readable_size(), 0U);
  EXPECT_TRUE(stream.state().end_received);
  stream.process(unexpected_data, ignore_state);
}

TEST(TcpStreamReassembly, RejectsConflictingOrTruncatingEndsAtomically) {
  tcp_stream_reassembly stream(128);
  anchor(stream, 100);
  feed(stream, input(110, "abc"), unexpected_data, ignore_state);
  EXPECT_EQ(feed(stream, input(100, "XYZ", 112), unexpected_data, ignore_state)
                .status,
            tcp_insert_status::invalid_end);
  EXPECT_EQ(stream.buffered_size(), 3U);
  EXPECT_EQ(stream.readable_size(), 0U);
  EXPECT_FALSE(stream.state().end_sequence.has_value());
  feed(stream, input(0, {}, 113), unexpected_data, ignore_state);
  EXPECT_EQ(
      feed(stream, input(0, {}, 114), unexpected_data, ignore_state).status,
      tcp_insert_status::invalid_end);
  EXPECT_EQ(
      feed(stream, input(0, {}, 99), unexpected_data, ignore_state).status,
      tcp_insert_status::invalid_end);
  EXPECT_EQ(feed(stream, input(0, {}, 100U + (uint32_t{1} << 31)),
                 unexpected_data, ignore_state)
                .status,
            tcp_insert_status::invalid_end);
  EXPECT_EQ(stream.state().end_sequence, 113U);
}

TEST(TcpStreamReassembly,
     InvalidConsumerResultDoesNotUndoInsertionOrReleaseData) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  const auto result = feed(
      stream, input(100, "abc"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        return buffers.size() + 1;
      },
      ignore_state);
  EXPECT_EQ(result.status, tcp_insert_status::accepted);
  EXPECT_EQ(result.delivery.status, tcp_callback_status::invalid_remaining);
  EXPECT_EQ(result.delivery.remaining_bytes, 3U);
  EXPECT_EQ(result.delivery.consumed_bytes, 0U);
  EXPECT_EQ(stream.state().next_sequence, 100U);
  stream.process(
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), "abc");
        return size_t{0};
      },
      ignore_state);
}

TEST(TcpStreamReassembly,
     CallbackExceptionsPreserveDataAndRetryGapNotification) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  auto throw_data = [](const net::readable_buffers&,
                       net::tcp_stream_status) -> size_t {
    throw std::runtime_error("consumer failed");
  };
  EXPECT_THROW(feed(stream, input(100, "abc"), throw_data, ignore_state),
               std::runtime_error);
  EXPECT_EQ(stream.buffered_size(), 3U);
  EXPECT_EQ(stream.state().next_sequence, 100U);
  auto throw_state = [](const tcp_stream_state&) {
    throw std::runtime_error("notify failed");
  };
  EXPECT_THROW(feed(stream, input(110, "xyz"), keep_data, throw_state),
               std::runtime_error);
  size_t notifications = 0;
  stream.process(keep_data, [&](const tcp_stream_state& event) {
    ++notifications;
    EXPECT_TRUE(event.gap.has_value());
  });
  EXPECT_EQ(notifications, 1U);
  EXPECT_EQ(stream.buffered_size(), 6U);
}

TEST(TcpStreamReassembly, RejectsCallbackReentryButAllowsStateInspection) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  feed(
      stream, input(100, "abc"),
      [&](const net::readable_buffers&, net::tcp_stream_status) {
        EXPECT_EQ(stream.state().readable_bytes, 3U);
        EXPECT_THROW(stream.clear(), std::logic_error);
        EXPECT_THROW(feed(stream, input(103, "x"), keep_data, ignore_state),
                     std::logic_error);
        EXPECT_THROW(stream.process(keep_data, ignore_state), std::logic_error);
        EXPECT_THROW(stream.skip_gap(), std::logic_error);
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(stream.buffered_size(), 0U);
  stream.clear();
  anchor(stream, 200);
  EXPECT_EQ(stream.state().next_sequence, 200U);
}

TEST(TcpStreamReassembly, ClearReusesAllocationAndClearsCoverageEndAndHistory) {
  tcp_stream_reassembly stream(64);
  anchor(stream, 100);
  const uint8_t* original = nullptr;
  feed(
      stream, input(100, "abc"),
      [&](const net::readable_buffers& buffers, net::tcp_stream_status) {
        original = buffers.first.data;
        return buffers.size();
      },
      ignore_state);
  feed(stream, input(110, "xyz", 113), keep_data, ignore_state);
  stream.skip_gap();
  stream.clear();
  EXPECT_FALSE(stream.state().initialized);
  EXPECT_FALSE(stream.state().gap.has_value());
  EXPECT_FALSE(stream.state().end_sequence.has_value());
  EXPECT_FALSE(stream.state().had_skipped_gap);
  feed(
      stream, input(500, "NEW"),
      [&](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(buffers.first.data, original);
        EXPECT_EQ(flatten(buffers), "NEW");
        return size_t{0};
      },
      ignore_state);
}

// This intentionally simple byte-by-byte model is independent of the ring and
// word-bitmap implementation. Conflicting overlaps retain the first arrival.
TEST(TcpStreamReassembly,
     MatchesReferenceUnderRandomOverlapConsumptionAndSkipping) {
  constexpr size_t iterations = 2000;
  for (const size_t capacity : {size_t{64}, size_t{128}, size_t{256}}) {
    for (const uint32_t seed : {1U, 17U, 991U}) {
      SCOPED_TRACE(::testing::Message()
                   << "capacity=" << capacity << " seed=" << seed);
      std::mt19937 random(seed);
      constexpr uint32_t base = 0xffffffe0U;
      tcp_stream_reassembly stream(capacity);
      anchor(stream, base);
      std::vector<std::optional<uint8_t>> reference((iterations * 64) + 1024);
      size_t head = 0;
      size_t buffered = 0;
      bool had_skipped_gap = false;
      std::vector<tcp_stream_state> events;
      auto notify = [&](const tcp_stream_state& event) {
        events.push_back(event);
      };
      auto reference_ready = [&] {
        size_t end = head;
        while (reference.at(end).has_value()) {
          ++end;
        }
        return end;
      };
      auto reference_gap = [&]() -> std::optional<tcp_gap_info> {
        const size_t ready = reference_ready();
        size_t first = ready;
        while (first < head + capacity && !reference.at(first).has_value()) {
          ++first;
        }
        if (first == head + capacity) {
          return std::nullopt;
        }
        size_t end = first;
        while (reference.at(end).has_value()) {
          ++end;
        }
        return tcp_gap_info{base + static_cast<uint32_t>(ready),
                            base + static_cast<uint32_t>(first),
                            first - ready,
                            end - first,
                            buffered - (ready - head),
                            true};
      };
      auto verify = [&](bool processed = true) {
        const auto state = stream.state();
        EXPECT_EQ(state.next_sequence, base + static_cast<uint32_t>(head));
        EXPECT_EQ(state.readable_bytes, reference_ready() - head);
        EXPECT_EQ(state.buffered_bytes, buffered);
        const auto expected_gap = reference_gap();
        EXPECT_EQ(state.gap, expected_gap);
        tcp_stream_state expected;
        expected.initialized = true;
        expected.next_sequence = base + static_cast<uint32_t>(head);
        expected.readable_bytes = reference_ready() - head;
        expected.buffered_bytes = buffered;
        expected.had_skipped_gap = had_skipped_gap;
        expected.gap = expected_gap;
        expected.status = expected.readable_bytes != 0
                              ? net::tcp_stream_status::data_ready
                              : net::tcp_stream_status::waiting_data;
        if (expected_gap.has_value()) {
          expected.status = net::tcp_stream_status::gap_waiting;
        }
        EXPECT_EQ(state, expected);
        ASSERT_EQ(events.size(), processed ? 1U : 0U);
        if (processed) {
          EXPECT_EQ(events.back(), expected);
        }
        events.clear();
      };

      for (size_t iteration = 0; iteration < iterations; ++iteration) {
        size_t start = reference_ready();
        if (iteration % 4 != 0) {
          const int64_t candidate =
              static_cast<int64_t>(head) +
              static_cast<int64_t>(random() % (capacity + 40)) - 20;
          start = static_cast<size_t>(std::max(int64_t{0}, candidate));
        }
        const size_t length = 1 + (random() % 60);
        std::string payload(length, '\0');
        for (auto& value : payload) {
          value = static_cast<char>(random() % 256);
        }
        const size_t clipped_start = std::max(start, head);
        const size_t clipped_end = std::max(start + length, head);
        const bool rejected = clipped_end > head + capacity;
        size_t added = 0;
        if (!rejected) {
          for (size_t position = clipped_start; position < clipped_end;
               ++position) {
            if (!reference.at(position).has_value()) {
              reference.at(position) =
                  static_cast<uint8_t>(payload.at(position - start));
              ++added;
            }
          }
          buffered += added;
        }
        size_t data_calls = 0;
        const auto result = feed(
            stream, input(base + static_cast<uint32_t>(start), payload),
            [&](const net::readable_buffers& buffers, net::tcp_stream_status) {
              ++data_calls;
              EXPECT_FALSE(rejected);
              const size_t ready = reference_ready();
              EXPECT_EQ(buffers.size(), ready - head);
              const std::string actual = flatten(buffers);
              for (size_t index = 0; index < actual.size(); ++index) {
                EXPECT_EQ(static_cast<uint8_t>(actual.at(index)),
                          *reference.at(head + index));
              }
              const size_t consumed = random() % (buffers.size() + 1);
              for (size_t index = 0; index < consumed; ++index) {
                reference.at(head + index).reset();
              }
              head += consumed;
              buffered -= consumed;
              return buffers.size() - consumed;
            },
            notify);
        if (rejected) {
          EXPECT_EQ(result.status, tcp_insert_status::out_of_window);
          EXPECT_EQ(data_calls, 0U);
        } else {
          EXPECT_EQ(result.status, added == 0 ? tcp_insert_status::duplicate
                                              : tcp_insert_status::accepted);
          EXPECT_EQ(result.new_bytes, added);
          EXPECT_EQ(result.overlap_bytes, length - added);
        }
        verify(!rejected);

        if (iteration % 37 == 0 && reference_gap().has_value()) {
          const size_t ready = reference_ready();
          const size_t missing = reference_gap()->missing_bytes;
          for (size_t position = head; position < ready; ++position) {
            reference.at(position).reset();
          }
          buffered -= ready - head;
          const size_t discarded = ready - head;
          head = ready + missing;
          const auto skipped = stream.skip_gap();
          had_skipped_gap = true;
          stream.process(keep_data, notify);
          EXPECT_EQ(skipped.missing_bytes, missing);
          EXPECT_EQ(skipped.discarded_ready_bytes, discarded);
          verify();
        }
      }
    }
  }
}

TEST(TcpStreamReassembly, SuppliesCurrentStatusToDataAndStateCallbacks) {
  tcp_stream_reassembly stream(64);
  EXPECT_EQ(stream.status(), net::tcp_stream_status::uninitialized);
  feed(
      stream, input(100, "abc"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status status) {
        EXPECT_EQ(status, net::tcp_stream_status::data_ready);
        return buffers.size();
      },
      ignore_state);
  feed(
      stream, input(110, "xyz"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status status) {
        EXPECT_EQ(status, net::tcp_stream_status::gap_waiting);
        return buffers.size();
      },
      [](const tcp_stream_state& event) {
        EXPECT_TRUE(event.gap.has_value());
        EXPECT_EQ(event.status, net::tcp_stream_status::gap_waiting);
      });
  feed(
      stream, input(103, "defghij"),
      [](const net::readable_buffers& buffers, net::tcp_stream_status status) {
        EXPECT_EQ(status, net::tcp_stream_status::data_ready);
        EXPECT_EQ(buffers.size(), 13U);
        return size_t{0};
      },
      [](const tcp_stream_state& event) {
        EXPECT_FALSE(event.gap.has_value());
        EXPECT_EQ(event.status, net::tcp_stream_status::waiting_data);
      });
  EXPECT_EQ(stream.state().status, net::tcp_stream_status::waiting_data);
  feed(
      stream, input(113, "last", 117),
      [](const net::readable_buffers&, net::tcp_stream_status status) {
        EXPECT_EQ(status, net::tcp_stream_status::end_received);
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(stream.status(), net::tcp_stream_status::end_received);
  stream.clear();
  EXPECT_EQ(stream.status(), net::tcp_stream_status::uninitialized);
}

TEST(TcpStreamReassembly,
     BatchesInsertsAndOnlyProcessesWhenExplicitlyRequested) {
  tcp_stream_reassembly stream(64);
  EXPECT_EQ(stream.insert(input(100, "abc")).new_bytes, 3U);
  EXPECT_EQ(stream.insert(input(106, "ghi")).new_bytes, 3U);
  EXPECT_EQ(stream.insert(input(103, "def")).new_bytes, 3U);
  EXPECT_EQ(stream.insert(input(100, "XXXXXXXXX")).status,
            tcp_insert_status::duplicate);
  EXPECT_EQ(stream.state().next_sequence, 100U);
  EXPECT_EQ(stream.readable_size(), 9U);
  size_t data_calls = 0;
  size_t state_calls = 0;
  const auto result = stream.process(
      [&](const net::readable_buffers& buffers, net::tcp_stream_status status) {
        ++data_calls;
        EXPECT_EQ(flatten(buffers), "abcdefghi");
        EXPECT_EQ(status, net::tcp_stream_status::data_ready);
        return size_t{2};
      },
      [&](const tcp_stream_state& snapshot) {
        ++state_calls;
        EXPECT_EQ(snapshot.next_sequence, 107U);
        EXPECT_EQ(snapshot.readable_bytes, 2U);
        EXPECT_EQ(snapshot.status, net::tcp_stream_status::data_ready);
      });
  EXPECT_EQ(data_calls, 1U);
  EXPECT_EQ(state_calls, 1U);
  EXPECT_TRUE(result.state_notified);
  EXPECT_EQ(result.consumed_bytes, 7U);
  EXPECT_EQ(result.remaining_bytes, 2U);
  EXPECT_TRUE(stream
                  .process(keep_data,
                           [&](const tcp_stream_state& snapshot) {
                             ++state_calls;
                             EXPECT_EQ(snapshot.readable_bytes, 2U);
                           })
                  .state_notified);
  EXPECT_EQ(state_calls, 2U);
}

TEST(TcpStreamReassembly,
     CoalescesIntermediateBatchStatesAndReportsLatestSnapshot) {
  tcp_stream_reassembly stream(64);
  std::vector<tcp_stream_state> events;
  auto notify = [&](const tcp_stream_state& snapshot) {
    events.push_back(snapshot);
  };
  EXPECT_TRUE(stream.process(unexpected_data, notify).state_notified);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.back().status, net::tcp_stream_status::uninitialized);
  EXPECT_TRUE(stream.process(unexpected_data, notify).state_notified);
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(events.at(0), events.at(1));
  stream.insert(input(100));
  stream.insert(input(110, "xyz"));
  EXPECT_TRUE(stream.state().gap.has_value());
  stream.insert(input(100, "abcdefghij"));
  EXPECT_FALSE(stream.state().gap.has_value());
  EXPECT_EQ(events.size(), 2U);
  EXPECT_TRUE(stream.process(keep_data, notify).state_notified);
  ASSERT_EQ(events.size(), 3U);
  EXPECT_EQ(events.back().status, net::tcp_stream_status::data_ready);
  EXPECT_FALSE(events.back().gap.has_value());
  EXPECT_EQ(events.back().buffered_bytes, 13U);
  EXPECT_TRUE(stream.process(keep_data, notify).state_notified);
  ASSERT_EQ(events.size(), 4U);
  EXPECT_EQ(events.at(2), events.at(3));
}

TEST(TcpStreamReassembly, DefersSkipAndClearNotificationsUntilProcess) {
  tcp_stream_reassembly stream(64);
  std::vector<tcp_stream_state> events;
  auto notify = [&](const tcp_stream_state& snapshot) {
    events.push_back(snapshot);
  };
  stream.insert(input(100));
  stream.insert(input(105, "x"));
  stream.process(unexpected_data, notify);
  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(events.back().gap.has_value());
  stream.skip_gap();
  EXPECT_EQ(events.size(), 1U);
  stream.process(keep_data, notify);
  ASSERT_EQ(events.size(), 2U);
  EXPECT_FALSE(events.back().gap.has_value());
  EXPECT_TRUE(events.back().had_skipped_gap);
  EXPECT_EQ(events.back().readable_bytes, 1U);
  stream.clear();
  EXPECT_EQ(events.size(), 2U);
  stream.process(unexpected_data, notify);
  ASSERT_EQ(events.size(), 3U);
  EXPECT_EQ(events.back().status, net::tcp_stream_status::uninitialized);
  EXPECT_FALSE(events.back().had_skipped_gap);
}

TEST(TcpStreamReassembly, FullBatchRequiresExplicitProcessingBeforeRetry) {
  tcp_stream_reassembly stream(64);
  stream.insert(input(100, std::string(64, 'A')));
  EXPECT_EQ(stream.insert(input(164, "BC")).status,
            tcp_insert_status::out_of_window);
  EXPECT_EQ(stream.buffered_size(), 64U);
  EXPECT_EQ(stream.state().next_sequence, 100U);
  stream.process([](const net::readable_buffers&,
                    net::tcp_stream_status) { return size_t{32}; },
                 ignore_state);
  EXPECT_EQ(stream.insert(input(164, "BC")).status,
            tcp_insert_status::accepted);
  stream.process(
      [](const net::readable_buffers& buffers, net::tcp_stream_status) {
        EXPECT_EQ(flatten(buffers), std::string(32, 'A') + "BC");
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(stream.buffered_size(), 0U);
}

TEST(TcpStreamReassembly, StateCallbackCanInspectButCannotReenterTheStream) {
  tcp_stream_reassembly stream(64);
  stream.insert(input(100, "abc"));
  stream.process(
      [](const net::readable_buffers&, net::tcp_stream_status) {
        return size_t{0};
      },
      [&](const tcp_stream_state& snapshot) {
        EXPECT_EQ(stream.state(), snapshot);
        EXPECT_THROW(stream.insert(input(103, "x")), std::logic_error);
        EXPECT_THROW(stream.process(keep_data, ignore_state), std::logic_error);
        EXPECT_THROW(stream.skip_gap(), std::logic_error);
        EXPECT_THROW(stream.clear(), std::logic_error);
      });
  EXPECT_EQ(stream.insert(input(103, "x")).status, tcp_insert_status::accepted);
}

TEST(TcpStreamReassembly,
     StateNotificationFailureRetriesWithoutReplayingConsumedData) {
  tcp_stream_reassembly stream(64);
  stream.insert(input(100, "abc"));
  EXPECT_THROW(stream.process([](const net::readable_buffers&,
                                 net::tcp_stream_status) { return size_t{0}; },
                              [](const tcp_stream_state&) {
                                throw std::runtime_error("notify failed");
                              }),
               std::runtime_error);
  EXPECT_EQ(stream.buffered_size(), 0U);
  EXPECT_EQ(stream.state().next_sequence, 103U);
  size_t notifications = 0;
  auto notify = [&](const tcp_stream_state& snapshot) {
    ++notifications;
    EXPECT_EQ(snapshot.next_sequence, 103U);
    EXPECT_EQ(snapshot.status, net::tcp_stream_status::waiting_data);
  };
  EXPECT_TRUE(stream.process(unexpected_data, notify).state_notified);
  EXPECT_TRUE(stream.process(unexpected_data, notify).state_notified);
  EXPECT_EQ(notifications, 2U);
}

// NOLINTEND(readability-function-cognitive-complexity)
}  // namespace

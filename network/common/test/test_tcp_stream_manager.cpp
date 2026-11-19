#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tcp_four_tuple_filter.h"
#include "tcp_stream_manager.h"
#include "tcp_stream_reassembly.h"

namespace {

using net::tcp_connection_phase;
using net::tcp_finish_reason;
using net::tcp_manager_insert_status;
using net::tcp_release_status;
using net::tcp_stream_manager;

net::tcp_packet_view packet(uint32_t sequence = 100, std::string_view text = {},
                            uint8_t flags = 0, bool reverse = false,
                            uint32_t domain = 0) {
  // Deliberately view string bytes without allocating or copying.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
  net::tcp_packet_view result;
  result.domain_id = domain;
  result.tuple = reverse ? net::tcp_four_tuple{{2, 200}, {1, 100}}
                         : net::tcp_four_tuple{{1, 100}, {2, 200}};
  result.sequence = sequence;
  result.flags = flags;
  result.payload = {bytes, text.size()};
  return result;
}
constexpr net::tcp_four_tuple kForward{{1, 100}, {2, 200}};
constexpr net::tcp_four_tuple kReverse{{2, 200}, {1, 100}};
void allow_test_tuples(tcp_stream_manager& manager) {
  manager.replace_rules({{1, kForward}, {2, kReverse}});
}
std::string flatten(const net::readable_buffers& buffers) {
  std::string result;
  if (buffers.first.size != 0) {
    result.append(buffers.first.data, buffers.first.data + buffers.first.size);
  }
  if (buffers.second.size != 0) {
    result.append(buffers.second.data,
                  buffers.second.data + buffers.second.size);
  }
  return result;
}
const auto consume_data = [](const net::tcp_stream_context&,
                             const net::readable_buffers&,
                             net::tcp_stream_status) { return size_t{0}; };
const auto keep_data = [](const net::tcp_stream_context&,
                          const net::readable_buffers& buffers,
                          net::tcp_stream_status) { return buffers.size(); };
const auto ignore_state = [](const net::tcp_stream_context&,
                             const net::tcp_managed_stream_state&) {};
const auto unexpected_data = [](const net::tcp_stream_context&,
                                const net::readable_buffers& buffers,
                                net::tcp_stream_status) {
  ADD_FAILURE() << "Unexpected data delivery";
  return buffers.size();
};

// GTest assertion branches belong to complete behavioral scenarios.
// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST(TcpStreamManager, ValidatesConfigurationPacketsAndAdmission) {
  EXPECT_THROW(tcp_stream_manager({0, 64}), std::invalid_argument);
  EXPECT_THROW(tcp_stream_manager({2, 63}), std::invalid_argument);
  EXPECT_THROW(tcp_stream_manager({2, size_t{1} << 31}), std::invalid_argument);
  tcp_stream_manager manager({2, 64});
  EXPECT_EQ(manager.insert(packet(100, "x")).status,
            tcp_manager_insert_status::filtered);
  allow_test_tuples(manager);
  for (const uint8_t flags : {uint8_t{0}, uint8_t{net::tcp_ack},
                              uint8_t{net::tcp_fin}, uint8_t{net::tcp_rst}}) {
    EXPECT_EQ(manager.insert(packet(100, {}, flags)).status,
              tcp_manager_insert_status::not_creatable);
  }
  EXPECT_EQ(manager.insert(packet(100, "x", net::tcp_rst)).status,
            tcp_manager_insert_status::not_creatable);
  auto invalid = packet();
  invalid.payload = {nullptr, 1};
  EXPECT_EQ(manager.insert(invalid).status,
            tcp_manager_insert_status::invalid_input);
  invalid = packet(100, {}, net::tcp_syn | net::tcp_fin);
  EXPECT_EQ(manager.insert(invalid).status,
            tcp_manager_insert_status::invalid_input);
  EXPECT_EQ(manager.insert(packet(100, std::string(65, 'x'))).status,
            tcp_manager_insert_status::stream_rejected);
  EXPECT_EQ(manager.stats().connections, 0U);
  EXPECT_EQ(manager.stats().allocated_slots, 0U);
  tcp_stream_manager handshake_only({2, 64, false});
  allow_test_tuples(handshake_only);
  EXPECT_EQ(handshake_only.insert(packet(100, "x")).status,
            tcp_manager_insert_status::not_creatable);
  const auto syn = handshake_only.insert(packet(100, {}, net::tcp_syn));
  ASSERT_TRUE(syn.handle);
  EXPECT_EQ(handshake_only.insert(packet(900, "reverse", 0, true)).status,
            tcp_manager_insert_status::not_creatable);
  EXPECT_FALSE(handshake_only.state(*syn.handle)->started_midstream);
}

TEST(TcpStreamManager,
     SeparatesInsertionFromCallbacksAndAlwaysReportsSelectedState) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto inserted = manager.insert(packet(100, "abc"));
  ASSERT_TRUE(inserted.handle);
  size_t calls = 0;
  const auto state_callback =
      [&](const net::tcp_stream_context&,
          const net::tcp_managed_stream_state& snapshot) {
        ++calls;
        EXPECT_EQ(snapshot.stream.buffered_bytes, 0U);
      };
  EXPECT_EQ(calls, 0U);
  const auto processed =
      manager.process(*inserted.handle, consume_data, state_callback);
  EXPECT_EQ(processed.stream.consumed_bytes, 3U);
  EXPECT_TRUE(processed.stream.state_notified);
  manager.process(*inserted.handle, unexpected_data, state_callback);
  EXPECT_EQ(calls, 2U);
}

TEST(TcpStreamManager,
     ReassemblesOutOfOrderOverlapsAndExplicitlySkipsPermanentGap) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto syn = manager.insert(packet(99, {}, net::tcp_syn));
  ASSERT_TRUE(syn.handle);
  manager.insert(packet(106, "ghijk"));
  auto state = manager.state(*syn.handle).value();
  ASSERT_TRUE(state.stream.gap);
  EXPECT_EQ(state.stream.gap->missing_bytes, 6U);
  EXPECT_EQ(state.stream.gap->contiguous_after_gap_bytes, 5U);
  manager.insert(packet(100, "abc"));
  const auto overlap = manager.insert(packet(107, "XXXXX"));
  EXPECT_EQ(overlap.stream.overlap_bytes, 4U);
  EXPECT_EQ(overlap.stream.new_bytes, 1U);
  manager.process(*syn.handle, keep_data, ignore_state);
  const auto skipped = manager.skip_gap(*syn.handle);
  EXPECT_TRUE(skipped.valid_handle);
  EXPECT_EQ(skipped.stream.missing_bytes, 3U);
  EXPECT_EQ(skipped.stream.discarded_ready_bytes, 3U);
  std::string output;
  manager.process(
      *syn.handle,
      [&](const net::tcp_stream_context&, const net::readable_buffers& buffers,
          net::tcp_stream_status) {
        output = flatten(buffers);
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(output, "ghijkX");
  EXPECT_TRUE(manager.state(*syn.handle)->stream.had_skipped_gap);
}

TEST(TcpStreamManager, LateSynKeepsMidstreamBaselineAndOrigin) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto first = manager.insert(packet(110, "tail"));
  const auto late_syn = manager.insert(packet(99, {}, net::tcp_syn));
  EXPECT_EQ(first.handle, late_syn.handle);
  EXPECT_FALSE(late_syn.created);
  const auto state = manager.state(*first.handle).value();
  EXPECT_TRUE(state.started_midstream);
  EXPECT_EQ(state.stream.next_sequence, 110U);
  EXPECT_EQ(state.syn_sequence, 99U);
}

TEST(TcpStreamManager,
     DifferentSynCreatesNewHandleAndReleasingOldKeepsNewMapping) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto old = manager.insert(packet(99, "old", net::tcp_syn));
  const auto fresh = manager.insert(packet(999, "new", net::tcp_syn));
  EXPECT_TRUE(fresh.created);
  ASSERT_TRUE(fresh.handle);
  EXPECT_FALSE(old.handle == fresh.handle);
  EXPECT_EQ(manager.state(*old.handle)->reason, tcp_finish_reason::replaced);
  manager.process(*old.handle, consume_data, ignore_state);
  manager.process(*fresh.handle, consume_data, ignore_state);
  EXPECT_EQ(manager.release(*old.handle).status, tcp_release_status::released);
  const auto next = manager.insert(packet(1003, "next"));
  EXPECT_EQ(next.handle, fresh.handle);
  EXPECT_EQ(manager.stats().connections, 1U);
}

TEST(TcpStreamManager, FullTableRejectsReplacementWithoutMutatingOriginal) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto old = manager.insert(packet(99, "old", net::tcp_syn));
  EXPECT_EQ(manager.insert(packet(199, "new", net::tcp_syn)).status,
            tcp_manager_insert_status::connection_limit);
  EXPECT_EQ(manager.state(*old.handle)->phase, tcp_connection_phase::tracking);
  EXPECT_EQ(manager.insert(packet(103, "tail")).handle, old.handle);
  EXPECT_EQ(manager.stats().capacity_rejections, 1U);
}

TEST(TcpStreamManager, ActivityRemainsMonotonicWithoutImplicitExpiry) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  auto incoming = packet(100, "one");
  incoming.timestamp_us = 100;
  const auto first = manager.insert(incoming);
  incoming.domain_id = 1;
  incoming.timestamp_us = 1'000'000'000;
  const auto second = manager.insert(incoming);
  EXPECT_EQ(manager.state(*first.handle)->last_seen_us, 100U);
  EXPECT_EQ(manager.state(*first.handle)->phase,
            tcp_connection_phase::tracking);
  incoming.domain_id = 0;
  incoming.timestamp_us = 90;
  const auto duplicate = manager.insert(incoming);
  EXPECT_EQ(duplicate.status, tcp_manager_insert_status::handled);
  EXPECT_EQ(duplicate.stream.new_bytes, 0U);
  EXPECT_EQ(manager.state(*first.handle)->last_seen_us, 1'000'000'000U);
  EXPECT_EQ(manager.state(*second.handle)->phase,
            tcp_connection_phase::tracking);
  EXPECT_EQ(manager.finish_all(), 2U);
}

TEST(TcpStreamManager, FinishAllScansLiveSlotsAfterHolesAndReuse) {
  tcp_stream_manager manager({5, 64});
  allow_test_tuples(manager);
  std::array<net::tcp_connection_handle, 5> handles{};
  for (uint32_t domain = 0; domain != handles.size(); ++domain) {
    const auto created =
        manager.insert(packet(99, {}, net::tcp_syn, false, domain));
    ASSERT_TRUE(created.handle);
    handles.at(domain) = *created.handle;
  }
  // Leave slot zero empty and reuse slot one with a different domain.
  for (const size_t domain : {size_t{0}, size_t{1}}) {
    manager.insert(
        packet(100, {}, net::tcp_rst, false, static_cast<uint32_t>(domain)));
    manager.process(handles.at(domain), unexpected_data, ignore_state);
    ASSERT_EQ(manager.release(handles.at(domain)).status,
              tcp_release_status::released);
  }
  const auto reused = manager.insert(packet(99, {}, net::tcp_syn, false, 5));
  ASSERT_TRUE(reused.handle);
  EXPECT_EQ(reused.handle->slot, handles.at(1).slot);
  EXPECT_NE(reused.handle->generation, handles.at(1).generation);
  EXPECT_FALSE(manager.state(handles.at(0)));
  EXPECT_FALSE(manager.state(handles.at(1)));
  manager.insert(packet(100, {}, net::tcp_fin, false, 3));
  manager.process(handles.at(3), unexpected_data, ignore_state);

  EXPECT_EQ(manager.finish_all(tcp_finish_reason::capture_error), 3U);
  EXPECT_EQ(manager.finish_all(), 0U);
  EXPECT_EQ(manager.state(handles.at(3))->reason,
            tcp_finish_reason::fin_observed);
  EXPECT_EQ(manager.state(handles.at(2))->reason,
            tcp_finish_reason::capture_error);
  EXPECT_EQ(manager.state(handles.at(4))->reason,
            tcp_finish_reason::capture_error);
  EXPECT_EQ(manager.state(*reused.handle)->reason,
            tcp_finish_reason::capture_error);
  size_t notified = 0;
  for (const auto handle : {handles.at(2), handles.at(4), *reused.handle}) {
    manager.process(handle, unexpected_data,
                    [&](const net::tcp_stream_context&,
                        const net::tcp_managed_stream_state& state) {
                      ++notified;
                      EXPECT_EQ(state.reason, tcp_finish_reason::capture_error);
                    });
  }
  EXPECT_EQ(notified, 3U);
  for (const auto handle :
       {handles.at(2), handles.at(3), handles.at(4), *reused.handle}) {
    EXPECT_EQ(manager.release(handle).status, tcp_release_status::released);
  }
  EXPECT_EQ(manager.stats().connections, 0U);
  EXPECT_EQ(manager.stats().allocated_slots, 5U);
}

TEST(TcpStreamManager, FinishAllOnEmptyOrAlreadyEndedRecordsIsIdempotent) {
  tcp_stream_manager empty({1, 64});
  EXPECT_EQ(empty.finish_all(), 0U);
  EXPECT_EQ(empty.finish_all(), 0U);
  EXPECT_EQ(empty.stats().allocated_slots, 0U);
  EXPECT_TRUE(empty.stats().source_finished);

  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto flow = manager.insert(packet(99, {}, net::tcp_syn));
  manager.insert(packet(100, {}, net::tcp_rst));
  EXPECT_EQ(manager.finish_all(tcp_finish_reason::manual), 0U);
  EXPECT_EQ(manager.state(*flow.handle)->reason,
            tcp_finish_reason::rst_observed);
  manager.process(*flow.handle, unexpected_data, ignore_state);
  EXPECT_EQ(manager.release(*flow.handle).status, tcp_release_status::released);
}

TEST(TcpStreamManager,
     ReusedSlotRetainsBuffersAndRejectsStaleHandleEverywhere) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto first = manager.insert(packet(100, "old"));
  manager.insert(packet(103, {}, net::tcp_rst));
  manager.process(*first.handle, consume_data, ignore_state);
  ASSERT_EQ(manager.release(*first.handle).status,
            tcp_release_status::released);
  const auto second = manager.insert(packet(1000, "new"));
  EXPECT_EQ(first.handle->slot, second.handle->slot);
  EXPECT_NE(first.handle->generation, second.handle->generation);
  EXPECT_EQ(manager.stats().allocated_slots, 1U);
  EXPECT_FALSE(manager.state(*first.handle));
  EXPECT_FALSE(manager.skip_gap(*first.handle).valid_handle);
  EXPECT_EQ(manager.release(*first.handle).status,
            tcp_release_status::invalid_handle);
  EXPECT_EQ(
      manager.process(*first.handle, unexpected_data, ignore_state).status,
      net::tcp_manager_process_status::invalid_handle);
  EXPECT_EQ(manager.state(*second.handle)->stream.buffered_bytes, 3U);
}

TEST(TcpStreamManager, ReentrantMutationsAreRejectedButQueriesWork) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto first = manager.insert(packet(100, "abc"));
  manager.process(
      *first.handle,
      [&](const net::tcp_stream_context&, const net::readable_buffers&,
          net::tcp_stream_status) {
        EXPECT_TRUE(manager.state(*first.handle));
        EXPECT_EQ(manager.stats().connections, 1U);
        EXPECT_THROW(manager.insert(packet(103, "x")), std::logic_error);
        EXPECT_THROW(manager.replace_rules({}), std::logic_error);
        EXPECT_THROW(manager.skip_gap(*first.handle), std::logic_error);
        EXPECT_THROW(manager.finish_all(), std::logic_error);
        EXPECT_THROW(manager.release(*first.handle), std::logic_error);
        EXPECT_THROW(manager.process(*first.handle, consume_data, ignore_state),
                     std::logic_error);
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(manager.insert(packet(103, "x")).status,
            tcp_manager_insert_status::handled);
}

TEST(TcpStreamManager,
     RejectedStreamInputLeavesBytesAndFinUnchangedButRecordsRejection) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto first = manager.insert(packet(100, "abc"));
  const auto rejected = manager.insert(packet(164, "x", net::tcp_fin));
  EXPECT_EQ(rejected.status, tcp_manager_insert_status::stream_rejected);
  const auto state = manager.state(*first.handle).value();
  EXPECT_EQ(state.stream.buffered_bytes, 3U);
  EXPECT_FALSE(state.stream.end_sequence);
  EXPECT_FALSE(state.fin_sequence);
  EXPECT_EQ(state.rejected_inserts, 1U);
  EXPECT_EQ(state.last_insert_status, net::tcp_insert_status::out_of_window);
}

// Additional directional contracts follow.

TEST(TcpStreamManager, ExactRuleRejectsReverseBeforeAllocatingAnything) {
  tcp_stream_manager manager({4, 64});
  manager.replace_rules({{1, kForward}});
  const auto first = manager.insert(packet(100, "forward"));
  ASSERT_TRUE(first.handle);
  for (const uint8_t flags :
       {uint8_t{0}, net::tcp_syn, net::tcp_ack, net::tcp_fin, net::tcp_rst}) {
    const auto reversed = manager.insert(packet(900, "reverse", flags, true));
    EXPECT_EQ(reversed.status, tcp_manager_insert_status::filtered);
    EXPECT_FALSE(reversed.handle);
  }
  EXPECT_EQ(manager.stats().connections, 1U);
  EXPECT_EQ(manager.stats().allocated_slots, 1U);
  EXPECT_EQ(manager.state(*first.handle)->phase,
            tcp_connection_phase::tracking);
  EXPECT_EQ(manager.state(*first.handle)->stream.buffered_bytes, 7U);
  auto different_port = packet(100, "other");
  different_port.tuple.src.port = 101;
  EXPECT_EQ(manager.insert(different_port).status,
            tcp_manager_insert_status::filtered);
}

TEST(TcpStreamManager, FinEndsOnlyItsOwnDirectionWithoutWaitingForPeerAck) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto forward = manager.insert(packet(100, "abc"));
  const auto reverse = manager.insert(packet(900, "peer", 0, true));
  manager.insert(packet(103, {}, net::tcp_fin));
  const auto state = manager.state(*forward.handle).value();
  EXPECT_EQ(state.phase, tcp_connection_phase::terminated);
  EXPECT_EQ(state.reason, tcp_finish_reason::fin_observed);
  EXPECT_EQ(state.fin_sequence, 103U);
  EXPECT_TRUE(state.stream.end_received);
  EXPECT_EQ(manager.state(*reverse.handle)->phase,
            tcp_connection_phase::tracking);
  EXPECT_EQ(manager.release(*forward.handle).status,
            tcp_release_status::buffered_data);
  manager.process(*forward.handle, consume_data, ignore_state);
  EXPECT_EQ(manager.release(*forward.handle).status,
            tcp_release_status::released);
  EXPECT_EQ(manager.insert(packet(904, "more", 0, true)).handle,
            reverse.handle);
  EXPECT_EQ(manager.state(*reverse.handle)->stream.buffered_bytes, 8U);
}

TEST(TcpStreamManager, FinNotifiesGapAndAcceptsLateCaptureDataBeforeRelease) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto syn = manager.insert(packet(99, {}, net::tcp_syn));
  manager.insert(packet(103, {}, net::tcp_fin));
  size_t calls = 0;
  manager.process(*syn.handle, unexpected_data,
                  [&](const net::tcp_stream_context&,
                      const net::tcp_managed_stream_state& state) {
                    ++calls;
                    EXPECT_EQ(state.reason, tcp_finish_reason::fin_observed);
                    ASSERT_TRUE(state.stream.gap);
                    EXPECT_EQ(state.stream.gap->missing_bytes, 3U);
                    EXPECT_FALSE(state.stream.end_received);
                  });
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(manager.insert(packet(100, "abc")).status,
            tcp_manager_insert_status::handled);
  EXPECT_TRUE(manager.state(*syn.handle)->stream.end_received);
  EXPECT_EQ(manager.release(*syn.handle).status,
            tcp_release_status::buffered_data);
  EXPECT_EQ(manager.insert(packet(103, "beyond")).status,
            tcp_manager_insert_status::stream_rejected);
  manager.process(
      *syn.handle,
      [](const net::tcp_stream_context&, const net::readable_buffers& data,
         net::tcp_stream_status) {
        EXPECT_EQ(flatten(data), "abc");
        return size_t{0};
      },
      ignore_state);
  EXPECT_EQ(manager.release(*syn.handle).status, tcp_release_status::released);
}

TEST(TcpStreamManager, FinSequenceWrapAndConflictingEndAreHandledPerDirection) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto maximum = std::numeric_limits<uint32_t>::max();
  const auto flow = manager.insert(packet(maximum - 2, "ab"));
  manager.insert(packet(maximum, {}, net::tcp_fin));
  EXPECT_EQ(manager.state(*flow.handle)->fin_sequence, maximum);
  EXPECT_EQ(manager.state(*flow.handle)->stream.end_sequence, maximum);
  EXPECT_EQ(manager.insert(packet(maximum, {}, net::tcp_fin)).status,
            tcp_manager_insert_status::handled);
  EXPECT_EQ(manager.insert(packet(0, {}, net::tcp_fin)).status,
            tcp_manager_insert_status::stream_rejected);
  EXPECT_EQ(manager.state(*flow.handle)->fin_sequence, maximum);
  EXPECT_EQ(manager.state(*flow.handle)->reason,
            tcp_finish_reason::fin_observed);
}

TEST(TcpStreamManager, RstDoesNotModifyPeerAndIgnoresItsOwnPayload) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto forward = manager.insert(packet(99, {}, net::tcp_syn));
  const auto reverse = manager.insert(packet(900, "peer", 0, true));
  manager.insert(packet(104, "later"));
  manager.insert(packet(100, "ignored", net::tcp_rst));
  EXPECT_EQ(manager.state(*reverse.handle)->phase,
            tcp_connection_phase::tracking);
  manager.process(*forward.handle, unexpected_data, ignore_state);
  EXPECT_EQ(manager.state(*forward.handle)->reason,
            tcp_finish_reason::rst_observed);
  EXPECT_EQ(manager.insert(packet(100, "late")).status,
            tcp_manager_insert_status::connection_terminated);
  EXPECT_EQ(manager.release(*forward.handle).status,
            tcp_release_status::buffered_data);
  const auto released = manager.release(
      *forward.handle, net::tcp_release_policy::discard_buffered);
  EXPECT_EQ(released.status, tcp_release_status::released);
  EXPECT_EQ(released.discarded_bytes, 5U);
  EXPECT_TRUE(manager.state(*reverse.handle));
}

TEST(TcpStreamManager, AckAndSynRetransmissionsRemainInTheirExactTuple) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto syn = manager.insert(packet(99, "abc", net::tcp_syn));
  auto ack = packet(900, {}, net::tcp_ack, true);
  ack.acknowledgment = 103;
  EXPECT_EQ(manager.insert(ack).status,
            tcp_manager_insert_status::not_creatable);
  const auto duplicate = manager.insert(packet(99, "XXX", net::tcp_syn));
  EXPECT_EQ(duplicate.handle, syn.handle);
  EXPECT_EQ(duplicate.stream.overlap_bytes, 3U);
  const auto reverse =
      manager.insert(packet(899, {}, net::tcp_syn | net::tcp_ack, true));
  ASSERT_TRUE(reverse.handle);
  EXPECT_FALSE(reverse.handle == syn.handle);
  manager.insert(ack);
  EXPECT_EQ(manager.state(*reverse.handle)->last_acknowledgment, 103U);
  EXPECT_FALSE(manager.state(*syn.handle)->last_acknowledgment);
  EXPECT_EQ(manager.state(*syn.handle)->stream.next_sequence, 100U);
  manager.process(
      *syn.handle,
      [](const net::tcp_stream_context&, const net::readable_buffers& data,
         net::tcp_stream_status) {
        EXPECT_EQ(flatten(data), "abc");
        return size_t{0};
      },
      ignore_state);
}

TEST(TcpStreamManager, CapacityCountsEachDirectedTupleSeparately) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto forward = manager.insert(packet(100, "forward"));
  EXPECT_EQ(manager.insert(packet(900, "reverse", 0, true)).status,
            tcp_manager_insert_status::connection_limit);
  EXPECT_EQ(manager.stats().connections, 1U);
  EXPECT_EQ(manager.stats().allocated_slots, 1U);
  EXPECT_EQ(manager.state(*forward.handle)->stream.buffered_bytes, 7U);
}

TEST(TcpStreamManager,
     CaptureEndNeedsOnlyTheSelectedStreamsTerminalNotification) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto flow = manager.insert(packet(100, "abc"));
  EXPECT_EQ(manager.release(*flow.handle).status,
            tcp_release_status::still_active);
  EXPECT_THROW(manager.finish_all(tcp_finish_reason::none),
               std::invalid_argument);
  EXPECT_FALSE(manager.stats().source_finished);
  EXPECT_EQ(manager.finish_all(), 1U);
  EXPECT_EQ(manager.finish_all(), 0U);
  EXPECT_EQ(manager.insert(packet(103, "x")).status,
            tcp_manager_insert_status::source_finished);
  EXPECT_EQ(manager.release(*flow.handle).status,
            tcp_release_status::buffered_data);
  manager.process(*flow.handle, consume_data,
                  [](const net::tcp_stream_context&,
                     const net::tcp_managed_stream_state& state) {
                    EXPECT_EQ(state.reason, tcp_finish_reason::capture_end);
                    EXPECT_EQ(state.stream.buffered_bytes, 0U);
                  });
  EXPECT_EQ(manager.release(*flow.handle).status, tcp_release_status::released);
  EXPECT_EQ(manager.stats().connections, 0U);
}

TEST(TcpStreamManager, InsertImmediatelyProcessesOnlyTheCurrentDirectedStream) {
  tcp_stream_manager manager({3, 64});
  allow_test_tuples(manager);
  const auto other = manager.insert(packet(900, "peer", 0, true));
  size_t data_calls = 0;
  size_t state_calls = 0;
  net::tcp_connection_handle observed;
  const auto result = manager.insert(
      packet(100, "abc"),
      [&](const net::tcp_stream_context& context,
          const net::readable_buffers& data, net::tcp_stream_status status) {
        observed = context.handle;
        EXPECT_TRUE(context.tuple == kForward);
        EXPECT_EQ(context.domain_id, 0U);
        EXPECT_EQ(status, net::tcp_stream_status::data_ready);
        EXPECT_EQ(flatten(data), "abc");
        ++data_calls;
        return size_t{1};
      },
      [&](const net::tcp_stream_context& context,
          const net::tcp_managed_stream_state& state) {
        EXPECT_TRUE(context.handle == observed);
        EXPECT_EQ(state.stream.next_sequence, 102U);
        EXPECT_EQ(state.stream.buffered_bytes, 1U);
        EXPECT_TRUE(state.started_midstream);
        ++state_calls;
      });
  ASSERT_TRUE(result.handle);
  ASSERT_NE(result.context, nullptr);
  EXPECT_TRUE(*result.handle == observed);
  EXPECT_EQ(data_calls, 1U);
  EXPECT_EQ(state_calls, 1U);
  EXPECT_EQ(result.processing.consumed_bytes, 2U);
  EXPECT_EQ(result.processing.remaining_bytes, 1U);
  EXPECT_TRUE(result.processing.state_notified);
  EXPECT_EQ(manager.state(*other.handle)->stream.buffered_bytes, 4U);
  // Native core process can run outside the manager with its original
  // callbacks.
  EXPECT_EQ(
      result.context
          ->process(
              [](const net::readable_buffers& data, net::tcp_stream_status) {
                EXPECT_EQ(flatten(data), "c");
                return size_t{0};
              },
              [](const net::tcp_stream_state& state) {
                EXPECT_EQ(state.buffered_bytes, 0U);
              })
          .consumed_bytes,
      1U);
  EXPECT_EQ(manager.state(*result.handle)->stream.buffered_bytes, 0U);
}

TEST(TcpStreamManager, InsertWithoutCallbacksReturnsStableNativeContext) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto first = manager.insert(packet(100, "abc"));
  ASSERT_NE(first.context, nullptr);
  EXPECT_FALSE(first.processing.state_notified);
  EXPECT_EQ(first.context->buffered_size(), 3U);
  const auto reverse = manager.insert(packet(900, "other", 0, true));
  EXPECT_NE(first.context, reverse.context);
  const auto duplicate = manager.insert(packet(100, "XXX"));
  EXPECT_EQ(first.context, duplicate.context);
  EXPECT_EQ(first.handle, duplicate.handle);
  first.context->process(
      [](const net::readable_buffers& data, net::tcp_stream_status) {
        EXPECT_EQ(flatten(data), "abc");
        return size_t{0};
      },
      [](const net::tcp_stream_state&) {});
  manager.insert(packet(103, {}, net::tcp_rst));
  EXPECT_EQ(manager.release(*first.handle).status,
            tcp_release_status::released);
  EXPECT_FALSE(manager.state(*first.handle));
  const auto reused = manager.insert(packet(1000, "new"));
  EXPECT_NE(reused.handle->generation, first.handle->generation);
  EXPECT_EQ(manager.stats().allocated_slots, 2U);
  // The old raw pointer is expired after release, even if its address is
  // reused.
}

TEST(TcpStreamManager, RejectedInsertionsNeverProcessOrReturnNativeContext) {
  tcp_stream_manager manager({1, 64});
  size_t state_calls = 0;
  const auto count_state = [&](const net::tcp_stream_context&,
                               const net::tcp_managed_stream_state&) {
    ++state_calls;
  };
  EXPECT_EQ(
      manager.insert(packet(100, "x"), unexpected_data, count_state).status,
      tcp_manager_insert_status::filtered);
  allow_test_tuples(manager);
  auto invalid = packet();
  invalid.payload = {nullptr, 1};
  EXPECT_EQ(manager.insert(invalid, unexpected_data, count_state).status,
            tcp_manager_insert_status::invalid_input);
  EXPECT_EQ(
      manager
          .insert(packet(100, {}, net::tcp_ack), unexpected_data, count_state)
          .status,
      tcp_manager_insert_status::not_creatable);
  const auto first = manager.insert(packet(100, "abc"));
  const auto full = manager.insert(packet(900, "peer", 0, true),
                                   unexpected_data, count_state);
  EXPECT_EQ(full.status, tcp_manager_insert_status::connection_limit);
  EXPECT_EQ(full.context, nullptr);
  const auto outside =
      manager.insert(packet(164, "x"), unexpected_data, count_state);
  EXPECT_EQ(outside.status, tcp_manager_insert_status::stream_rejected);
  EXPECT_EQ(outside.context, nullptr);
  EXPECT_FALSE(outside.processing.state_notified);
  EXPECT_EQ(manager.state(*first.handle)->stream.buffered_bytes, 3U);
  EXPECT_EQ(state_calls, 0U);
}

TEST(TcpStreamManager,
     EverySuccessfulInsertProcessesStateIncludingDuplicatesAndControlPackets) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  size_t state_calls = 0;
  const auto count_state = [&](const net::tcp_stream_context&,
                               const net::tcp_managed_stream_state&) {
    ++state_calls;
  };
  const auto first = manager.insert(packet(99, {}, net::tcp_syn),
                                    unexpected_data, count_state);
  const auto data =
      manager.insert(packet(100, "abc"), consume_data, count_state);
  const auto duplicate =
      manager.insert(packet(100, "XXX"), unexpected_data, count_state);
  const auto ack = manager.insert(packet(103, {}, net::tcp_ack),
                                  unexpected_data, count_state);
  const auto fin =
      manager.insert(packet(103, {}, net::tcp_fin), unexpected_data,
                     [&](const net::tcp_stream_context&,
                         const net::tcp_managed_stream_state& state) {
                       ++state_calls;
                       EXPECT_EQ(state.reason, tcp_finish_reason::fin_observed);
                       EXPECT_TRUE(state.stream.end_received);
                     });
  EXPECT_EQ(state_calls, 5U);
  EXPECT_EQ(data.processing.consumed_bytes, 3U);
  EXPECT_EQ(first.context, data.context);
  EXPECT_EQ(duplicate.context, first.context);
  EXPECT_EQ(ack.context, first.context);
  EXPECT_EQ(fin.context, first.context);
  EXPECT_EQ(manager.release(*fin.handle).status, tcp_release_status::released);
}

TEST(TcpStreamManager,
     InvalidRemainingDoesNotConsumeAndNeverProcessesAnotherStream) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto result = manager.insert(
      packet(100, "abc"),
      [](const net::tcp_stream_context&, const net::readable_buffers& data,
         net::tcp_stream_status) { return data.size() + 1; },
      ignore_state);
  EXPECT_EQ(result.processing.status,
            net::tcp_callback_status::invalid_remaining);
  EXPECT_EQ(result.processing.consumed_bytes, 0U);
  EXPECT_EQ(result.context->buffered_size(), 3U);
  EXPECT_TRUE(result.processing.state_notified);
}

TEST(TcpStreamManager,
     CallbackExceptionsPreserveInsertionAndCompletedConsumption) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  size_t state_calls = 0;
  EXPECT_THROW(
      manager.insert(
          packet(100, "abc"),
          [](const net::tcp_stream_context&, const net::readable_buffers&,
             net::tcp_stream_status) -> size_t {
            throw std::runtime_error("data");
          },
          [&](const net::tcp_stream_context&,
              const net::tcp_managed_stream_state&) { ++state_calls; }),
      std::runtime_error);
  EXPECT_EQ(state_calls, 0U);
  const auto retry = manager.insert(packet(100, "XXX"));
  ASSERT_NE(retry.context, nullptr);
  EXPECT_EQ(retry.stream.new_bytes, 0U);
  EXPECT_EQ(retry.context->buffered_size(), 3U);
  EXPECT_THROW(manager.insert(packet(103, {}, net::tcp_ack), consume_data,
                              [](const net::tcp_stream_context&,
                                 const net::tcp_managed_stream_state&) {
                                throw std::runtime_error("state");
                              }),
               std::runtime_error);
  EXPECT_EQ(retry.context->buffered_size(), 0U);
  EXPECT_EQ(manager.insert(packet(103, "next"), consume_data, ignore_state)
                .processing.consumed_bytes,
            4U);
}

TEST(TcpStreamManager,
     NativeCoreCallbacksCannotReleaseOrReconfigureTheirActiveStorage) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto first = manager.insert(packet(100, "abc"));
  manager.insert(packet(103, {}, net::tcp_rst));
  first.context->process(
      [&](const net::readable_buffers&, net::tcp_stream_status) {
        EXPECT_TRUE(first.context->is_processing());
        EXPECT_THROW(manager.release(*first.handle,
                                     net::tcp_release_policy::discard_buffered),
                     std::logic_error);
        EXPECT_THROW(manager.replace_rules({}), std::logic_error);
        EXPECT_THROW(manager.finish_all(), std::logic_error);
        EXPECT_THROW(manager.insert(packet(999, {}, net::tcp_syn)),
                     std::logic_error);
        return size_t{0};
      },
      [](const net::tcp_stream_state&) {});
  EXPECT_FALSE(first.context->is_processing());
  EXPECT_EQ(manager.release(*first.handle).status,
            tcp_release_status::released);
}

TEST(TcpStreamManager,
     RuleRevocationDiscardsAndReportsCachedDataBeforeDirectProcessing) {
  tcp_stream_manager manager({2, 64});
  allow_test_tuples(manager);
  const auto forward = manager.insert(packet(100, "forward"));
  const auto reverse = manager.insert(packet(900, "secret", 0, true));
  const auto rejected = manager.replace_rules({{1, kForward}, {1, kReverse}});
  EXPECT_EQ(rejected.status, net::tcp_filter_update_status::duplicate_rule_id);
  EXPECT_EQ(rejected.discarded_bytes, 0U);
  EXPECT_EQ(reverse.context->buffered_size(), 6U);
  const auto revoked = manager.replace_rules({{1, kForward}});
  EXPECT_EQ(revoked.terminated_connections, 1U);
  EXPECT_EQ(revoked.discarded_bytes, 6U);
  EXPECT_EQ(reverse.context->buffered_size(), 0U);
  EXPECT_EQ(manager.state(*reverse.handle)->discarded_bytes, 6U);
  EXPECT_EQ(manager.state(*reverse.handle)->reason,
            tcp_finish_reason::filter_removed);
  reverse.context->process(
      [](const net::readable_buffers& data, net::tcp_stream_status) {
        ADD_FAILURE() << "Revoked payload exposed";
        return data.size();
      },
      [](const net::tcp_stream_state& state) {
        EXPECT_EQ(state.buffered_bytes, 0U);
      });
  EXPECT_EQ(forward.context->buffered_size(), 7U);
  EXPECT_EQ(manager.release(*reverse.handle).status,
            tcp_release_status::released);
  EXPECT_EQ(manager.insert(packet(107, "next"), consume_data, ignore_state)
                .processing.consumed_bytes,
            11U);
}

TEST(TcpStreamManager,
     MoveOnlyCallbacksAreBorrowedAndMayKeepAnUnconsumedSuffix) {
  tcp_stream_manager manager({1, 64});
  allow_test_tuples(manager);
  const auto result = manager.insert(
      packet(100, "abcd"),
      [counter = std::make_unique<size_t>(0)](const net::tcp_stream_context&,
                                              const net::readable_buffers& data,
                                              net::tcp_stream_status) mutable {
        ++*counter;
        return data.size() - *counter;
      },
      ignore_state);
  EXPECT_EQ(result.processing.consumed_bytes, 1U);
  EXPECT_EQ(result.context->readable_size(), 3U);
  EXPECT_EQ(manager.process(*result.handle, consume_data, ignore_state)
                .stream.consumed_bytes,
            3U);
}

TEST(TcpStreamManager,
     RandomImmediateInsertsKeepDirectionsDomainsAndCorePointersSeparate) {
  constexpr size_t kDomains = 40;
  tcp_stream_manager manager({kDomains * 2, 64});
  allow_test_tuples(manager);
  std::array<std::array<uint32_t, 2>, kDomains> sequences{};
  std::array<std::array<size_t, 2>, kDomains> observed{};
  std::array<std::array<size_t, 2>, kDomains> expected{};
  std::array<std::array<std::optional<net::tcp_connection_handle>, 2>, kDomains>
      handles{};
  std::array<std::array<net::tcp_stream_reassembly*, 2>, kDomains> contexts{};
  // Fixed seeds reproduce multiplexing errors; no security use.
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
  std::mt19937 random(71409);
  for (size_t operation = 0; operation != 4000; ++operation) {
    const auto domain = static_cast<uint32_t>(random() % kDomains);
    const size_t direction = random() % 2U;
    auto& sequence = sequences.at(domain).at(direction);
    const std::string text(1U + (random() % 16U),
                           static_cast<char>('A' + (domain % 26)));
    const auto incoming = packet(sequence, text, 0, direction != 0, domain);
    const auto result = manager.insert(
        incoming,
        [&](const net::tcp_stream_context& context,
            const net::readable_buffers& data, net::tcp_stream_status) {
          EXPECT_EQ(context.domain_id, domain);
          EXPECT_TRUE(context.tuple == (direction == 0 ? kForward : kReverse));
          EXPECT_EQ(flatten(data), text);
          observed.at(domain).at(direction) += data.size();
          return size_t{0};
        },
        ignore_state);
    ASSERT_EQ(result.status, tcp_manager_insert_status::handled);
    ASSERT_NE(result.context, nullptr);
    auto& handle = handles.at(domain).at(direction);
    auto& context = contexts.at(domain).at(direction);
    if (handle) {
      EXPECT_EQ(handle, result.handle);
      EXPECT_EQ(context, result.context);
    } else {
      handle = result.handle;
      context = result.context;
    }
    if (handles.at(domain).at(direction ^ 1U)) {
      EXPECT_FALSE(result.handle == handles.at(domain).at(direction ^ 1U));
      EXPECT_NE(result.context, contexts.at(domain).at(direction ^ 1U));
    }
    expected.at(domain).at(direction) += text.size();
    sequence += static_cast<uint32_t>(text.size());
    EXPECT_EQ(manager.insert(incoming, unexpected_data, ignore_state)
                  .stream.new_bytes,
              0U);
  }
  EXPECT_EQ(observed, expected);
  EXPECT_EQ(manager.finish_all(), kDomains * 2);
  for (const auto& domain : handles) {
    for (const auto handle : domain) {
      ASSERT_TRUE(handle);
      EXPECT_EQ(manager.release(*handle).status, tcp_release_status::released);
    }
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
}  // namespace

#include <gtest/gtest.h>
#include <pcap/dlt.h>
#include <pcap/pcap.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../../common/tcp_four_tuple_filter.h"
#include "../../../common/tcp_stream_manager.h"
#include "../../../common/tcp_stream_reassembly.h"
#include "../rx_pcap_tcp.h"

namespace {

using Bytes = std::vector<uint8_t>;
constexpr net::tcp_four_tuple kTuple{{0x0a000001, 12000}, {0x0a000002, 9000}};

void put16(Bytes& bytes, size_t offset, uint16_t value) {
  bytes.at(offset) = static_cast<uint8_t>(value >> 8);
  bytes.at(offset + 1) = static_cast<uint8_t>(value);
}

void put32(Bytes& bytes, size_t offset, uint32_t value) {
  put16(bytes, offset, static_cast<uint16_t>(value >> 16));
  put16(bytes, offset + 2, static_cast<uint16_t>(value));
}

uint16_t checksum(const Bytes& bytes, uint32_t sum = 0) {
  for (size_t index = 0; index < bytes.size(); index += 2) {
    sum += static_cast<uint32_t>(bytes.at(index)) << 8;
    if (index + 1 < bytes.size()) {
      sum += bytes.at(index + 1);
    }
  }
  while ((sum >> 16) != 0) {
    sum = (sum & 0xffffU) + (sum >> 16);
  }
  return static_cast<uint16_t>(~sum);
}

Bytes tcp(uint32_t sequence, uint8_t flags, const std::string& payload = "",
          net::tcp_four_tuple tuple = kTuple, bool options = false) {
  const size_t header_size = options ? 24 : 20;
  Bytes bytes(header_size, 0);
  put16(bytes, 0, tuple.src.port);
  put16(bytes, 2, tuple.dst.port);
  put32(bytes, 4, sequence);
  put32(bytes, 8, 77);
  bytes.at(12) = static_cast<uint8_t>((header_size / 4) << 4);
  bytes.at(13) = flags;
  put16(bytes, 14, 4096);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  const uint32_t pseudo = (tuple.src.ip >> 16) + (tuple.src.ip & 0xffffU) +
                          (tuple.dst.ip >> 16) + (tuple.dst.ip & 0xffffU) + 6 +
                          bytes.size();
  put16(bytes, 16, checksum(bytes, pseudo));
  return bytes;
}

Bytes ip(const Bytes& payload, uint16_t field = 0, uint16_t identification = 1,
         net::tcp_four_tuple tuple = kTuple, bool options = false) {
  const size_t header_size = options ? 24 : 20;
  Bytes bytes(header_size, 0);
  bytes.at(0) = static_cast<uint8_t>(0x40 | (header_size / 4));
  put16(bytes, 2, static_cast<uint16_t>(header_size + payload.size()));
  put16(bytes, 4, identification);
  put16(bytes, 6, field);
  bytes.at(8) = 64;
  bytes.at(9) = 6;
  put32(bytes, 12, tuple.src.ip);
  put32(bytes, 16, tuple.dst.ip);
  put16(bytes, 10, checksum(bytes));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

Bytes frame(uint32_t sequence, uint8_t flags, const std::string& payload = "",
            net::tcp_four_tuple tuple = kTuple) {
  return ip(tcp(sequence, flags, payload, tuple), 0, 1, tuple);
}

Bytes ethernet(const Bytes& packet, size_t tags = 0) {
  Bytes bytes(14 + (tags * 4), 0);
  put16(bytes, 12, tags == 0 ? 0x0800 : 0x8100);
  for (size_t index = 0; index < tags; ++index) {
    put16(bytes, 14 + (index * 4), static_cast<uint16_t>(index + 1));
    put16(bytes, 16 + (index * 4), index + 1 == tags ? 0x0800 : 0x8100);
  }
  bytes.insert(bytes.end(), packet.begin(), packet.end());
  return bytes;
}

class PcapTcpTest : public testing::Test {
 protected:
  void SetUp() override {
    std::array<char, 40> path{};
    const std::string pattern = "/tmp/net-test-pcap-tcp-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), path.begin());
    const int descriptor = mkstemp(path.data());
    ASSERT_GE(descriptor, 0);
    ASSERT_EQ(::close(descriptor), 0);
    path_ = path.data();
    cfg_.file_path = path_;
    cfg_.rules = {{1, kTuple}};
    cfg_.streams = {8, 128, true};
    cfg_.verify_checksums = true;
  }

  void TearDown() override {
    rx_.close();
    if (!path_.empty()) {
      EXPECT_EQ(std::remove(path_.c_str()), 0);
    }
  }

  void Write(const std::vector<Bytes>& frames, int datalink = DLT_RAW) {
    pcap_t* dead = pcap_open_dead(datalink, 65535);
    ASSERT_NE(dead, nullptr);
    pcap_dumper_t* dump = pcap_dump_open(dead, path_.c_str());
    if (dump == nullptr) {
      pcap_close(dead);
      FAIL() << "Cannot open pcap dump";
    }
    for (size_t index = 0; index < frames.size(); ++index) {
      pcap_pkthdr header{};
      header.ts.tv_sec = 1;
      header.ts.tv_usec = static_cast<decltype(header.ts.tv_usec)>(index);
      header.caplen = static_cast<bpf_u_int32>(frames.at(index).size());
      header.len = header.caplen;
      // libpcap's writer API uses an opaque u_char* for its dumper handle.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      pcap_dump(reinterpret_cast<uint8_t*>(dump), &header,
                frames.at(index).data());
    }
    pcap_dump_close(dump);
    pcap_close(dead);
  }

  void Open() {
    rx_.set_config(cfg_);
    ASSERT_TRUE(rx_.open()) << rx_.error();
  }

  std::string Read(net::tcp_stream_context& context, int capacity = 128) {
    std::array<char, 128> bytes{};
    const int count = rx_.read_from(context, bytes.data(), capacity);
    EXPECT_GT(count, 0) << rx_.error();
    return count > 0 ? std::string(bytes.data(), static_cast<size_t>(count))
                     : std::string{};
  }

  // GoogleTest subclasses access fixture state directly.
  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  std::string path_;
  net::rx_pcap_tcp_cfg_t cfg_;
  net::rx_pcap_tcp rx_;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// GoogleTest assertions expand into branches in these sequential checks.
// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_F(PcapTcpTest, ReassemblesOutOfOrderAndSuppressesRetransmissions) {
  Write({frame(100, net::tcp_syn), frame(104, net::tcp_ack, "def"),
         frame(101, net::tcp_ack, "abc"), frame(101, net::tcp_ack, "abc"),
         frame(107, net::tcp_fin | net::tcp_ack)});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "abcdef");
  EXPECT_EQ(context.tuple, kTuple);
  EXPECT_EQ(context.domain_id, 0U);
  std::array<char, 128> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -2);
  const auto state = rx_.streams()->state(context.handle);
  ASSERT_TRUE(state);
  EXPECT_EQ(state->reason, net::tcp_finish_reason::fin_observed);
  EXPECT_TRUE(state->stream.end_received);
  EXPECT_EQ(state->last_acknowledgment, 77U);
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -2);
}

TEST_F(PcapTcpTest, ShortReadsRetainSuffixBeforeAdvancingCapture) {
  Write({frame(10, net::tcp_ack, "abcdef"), frame(16, net::tcp_ack, "gh")});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context, 2), "ab");
  const auto handle = context.handle;
  EXPECT_EQ(rx_.read(nullptr, 1), -1);
  EXPECT_THROW(rx_.dispatch([](const net::tcp_stream_context&,
                               const net::readable_buffers&,
                               net::tcp_stream_status) { return 0U; },
                            [](const net::tcp_stream_context&,
                               const net::tcp_managed_stream_state&) {}),
               std::logic_error);
  EXPECT_EQ(Read(context, 3), "cde");
  EXPECT_TRUE(rx_.error().empty());
  EXPECT_EQ(Read(context, 3), "f");
  EXPECT_EQ(Read(context, 3), "gh");
  EXPECT_EQ(context.handle, handle);
  std::array<char, 8> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -2);
  EXPECT_EQ(rx_.streams()->state(handle)->reason,
            net::tcp_finish_reason::capture_end);
}

TEST_F(PcapTcpTest, KeepsDirectionsSeparateAndFiltersExactFourTuples) {
  const net::tcp_four_tuple reverse{kTuple.dst, kTuple.src};
  const net::tcp_four_tuple wrong_port{{kTuple.src.ip, 12001}, kTuple.dst};
  Write({frame(1, net::tcp_ack, "bad", wrong_port),
         frame(1, net::tcp_ack, "forward"),
         frame(1, net::tcp_ack, "reverse", reverse)});
  cfg_.rules.push_back({2, reverse});
  cfg_.domain_id = 5;
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "forward");
  const auto forward = context.handle;
  EXPECT_EQ(Read(context), "reverse");
  EXPECT_EQ(context.tuple, reverse);
  EXPECT_FALSE(context.handle == forward);
  EXPECT_EQ(context.domain_id, 5U);
  EXPECT_EQ(rx_.streams()->stats().filtered_packets, 1U);
}

TEST_F(PcapTcpTest, SkipsCompleteIpv4FragmentSets) {
  const auto segment =
      tcp(101, net::tcp_ack, "abcdefghijklmnopqrst", kTuple, true);
  const Bytes first(segment.begin(), segment.begin() + 32);
  const Bytes middle(segment.begin() + 32, segment.begin() + 40);
  const Bytes last(segment.begin() + 40, segment.end());
  Write({ip(last, 5, 7), ip(first, 0x2000, 7), ip(middle, 0x2004, 7)});
  // Even a first fragment containing an entire TCP header must be skipped
  // when checksum verification is disabled for offloaded captures.
  cfg_.verify_checksums = false;
  Open();
  std::array<char, 128> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -2);
  EXPECT_EQ(rx_.streams()->stats().connections, 0U);
  EXPECT_TRUE(rx_.error().empty());
}

TEST_F(PcapTcpTest, SkipsIpv4FragmentsWhosePayloadLooksLikeCompleteTcp) {
  for (const uint16_t field : {0x2000, 0x2001, 0x0001, 0x1fff}) {
    Write({ip(tcp(1, net::tcp_ack, "bad!"), field),
           frame(1, net::tcp_ack, "good")});
    Open();
    net::tcp_stream_context context;
    EXPECT_EQ(Read(context), "good") << "Fragment field: " << field;
    std::array<char, 8> buffer{};
    EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -2);
  }
}

TEST_F(PcapTcpTest, AcceptsEthernetVlanAndQinqWithPaddingAndIpv4Options) {
  auto packet = ethernet(ip(tcp(1, net::tcp_ack, "abc"), 0, 1, kTuple, true));
  packet.resize(packet.size() + 20, 0xff);
  Write({packet, ethernet(frame(4, net::tcp_ack, "def"), 1),
         ethernet(frame(7, net::tcp_ack, "ghi"), 2)},
        DLT_EN10MB);
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "abc");
  EXPECT_EQ(Read(context), "def");
  EXPECT_EQ(Read(context), "ghi");
}

TEST_F(PcapTcpTest, AcceptsLinuxCookedCapture) {
  for (const int datalink : {DLT_LINUX_SLL, DLT_LINUX_SLL2}) {
    rx_.close();
    const bool version_two = datalink == DLT_LINUX_SLL2;
    Bytes packet(version_two ? 20 : 16, 0);
    put16(packet, version_two ? 0 : 14, 0x0800);
    const auto raw = frame(1, net::tcp_ack, "cooked");
    packet.insert(packet.end(), raw.begin(), raw.end());
    Write({packet}, datalink);
    Open();
    net::tcp_stream_context context;
    EXPECT_EQ(Read(context), "cooked");
  }
}

TEST_F(PcapTcpTest, SkipsMalformedTruncatedAndNonTcpPackets) {
  const auto good = frame(1, net::tcp_ack, "good");
  auto truncated = good;
  truncated.pop_back();
  auto bad_ip_length = good;
  put16(bad_ip_length, 2, 10);
  auto bad_ip_header = good;
  bad_ip_header.at(0) = 0x44;
  auto bad_tcp_header = tcp(1, net::tcp_ack, "bad");
  bad_tcp_header.at(12) = 0xf0;
  auto short_tcp_header = tcp(1, net::tcp_ack, "bad");
  short_tcp_header.at(12) = 0x40;
  auto udp = good;
  udp.at(9) = 17;
  auto ipv6 = good;
  ipv6.at(0) = 0x65;
  Write({Bytes{}, Bytes{0x45}, truncated, bad_ip_length, bad_ip_header,
         ip(bad_tcp_header), ip(short_tcp_header), udp, ipv6,
         ip(tcp(1, net::tcp_ack, "bad"), 0x6000),
         frame(1, net::tcp_syn | net::tcp_fin, "bad"), good});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "good");
}

TEST_F(PcapTcpTest, VerifiesChecksumsAndAllowsOffloadedCapturesWhenDisabled) {
  auto bad_ip = frame(1, net::tcp_ack, "abc");
  bad_ip.at(10) ^= 1;
  auto bad_tcp = tcp(1, net::tcp_ack, "def");
  bad_tcp.at(16) ^= 1;
  Write({bad_ip, ip(bad_tcp), frame(1, net::tcp_ack, "good")});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "good");
  cfg_.verify_checksums = false;
  Open();
  EXPECT_EQ(Read(context), "abc");
}

TEST_F(PcapTcpTest, IncompleteAndOverlappingIpFragmentsAreNotDelivered) {
  const auto segment = tcp(1, net::tcp_ack, "abcdefgh");
  const Bytes first(segment.begin(), segment.begin() + 24);
  const Bytes overlap(segment.begin() + 16, segment.end());
  Write({ip(first, 0x2000, 7), ip(overlap, 2, 7), ip(first, 0x2000, 8),
         frame(1, net::tcp_ack, "good")});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "good");
}

TEST_F(PcapTcpTest, EofPreservesGapsAndAllowsExplicitRecovery) {
  Write({frame(100, net::tcp_syn), frame(104, net::tcp_ack, "def")});
  Open();
  std::optional<net::tcp_connection_handle> handle;
  int calls = 0;
  auto on_data = [&](const net::tcp_stream_context&,
                     const net::readable_buffers&, net::tcp_stream_status) {
    ++calls;
    return 0U;
  };
  auto on_state = [&](const net::tcp_stream_context& context,
                      const net::tcp_managed_stream_state&) {
    handle = context.handle;
  };
  EXPECT_EQ(rx_.dispatch(on_data, on_state), 1);
  EXPECT_EQ(rx_.dispatch(on_data, on_state), 1);
  EXPECT_EQ(rx_.dispatch(on_data, on_state), -2);
  ASSERT_TRUE(handle);
  const auto state = rx_.streams()->state(*handle);
  ASSERT_TRUE(state);
  EXPECT_EQ(state->reason, net::tcp_finish_reason::capture_end);
  ASSERT_TRUE(state->stream.gap);
  EXPECT_EQ(state->stream.gap->missing_bytes, 3U);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(rx_.streams()->skip_gap(*handle).stream.missing_bytes, 3U);
  std::string recovered;
  rx_.streams()->process(
      *handle,
      [&](const net::tcp_stream_context&, const net::readable_buffers& buffers,
          net::tcp_stream_status) {
        recovered.append(buffers.first.data,
                         buffers.first.data + buffers.first.size);
        return 0U;
      },
      on_state);
  EXPECT_EQ(recovered, "def");
  EXPECT_EQ(rx_.streams()->release(*handle).status,
            net::tcp_release_status::released);
}

TEST_F(PcapTcpTest, ReportsCaptureErrorAndFinishesExistingStreams) {
  Write({frame(1, net::tcp_ack, "abc"), frame(4, net::tcp_ack, "def")});
  std::filesystem::resize_file(path_, std::filesystem::file_size(path_) - 1);
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "abc");
  std::array<char, 8> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -1);
  EXPECT_FALSE(rx_.error().empty());
  EXPECT_EQ(rx_.streams()->state(context.handle)->reason,
            net::tcp_finish_reason::capture_error);
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -1);
}

TEST_F(PcapTcpTest, ReportsCapacityErrorsInsteadOfSilentlyLosingData) {
  const net::tcp_four_tuple second{{kTuple.src.ip, 12001}, kTuple.dst};
  Write({frame(1, net::tcp_ack, "abc"), frame(1, net::tcp_ack, "def", second)});
  cfg_.rules.push_back({2, second});
  cfg_.streams.max_connections = 1;
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "abc");
  std::array<char, 8> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -1);
  EXPECT_EQ(rx_.last_result().status,
            net::tcp_manager_insert_status::connection_limit);
  EXPECT_FALSE(rx_.error().empty());
}

TEST_F(PcapTcpTest, AcceptsUnfragmentedTcpOptionsAndDontFragmentFlag) {
  Write({ip(tcp(1, net::tcp_ack, "options", kTuple, true), 0x4000)});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "options");
}

TEST_F(PcapTcpTest, ReadsAcrossSequenceAndPhysicalBufferWrap) {
  cfg_.streams.stream_capacity = 64;
  Write({frame(0xfffffff0U, net::tcp_syn),
         frame(0xfffffff1U, net::tcp_ack, std::string(60, 'a')),
         frame(45, net::tcp_ack, "0123456789")});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), std::string(60, 'a'));
  EXPECT_EQ(Read(context), "0123456789");
}

TEST_F(PcapTcpTest, FillsGapAfterFinWithLatePayload) {
  Write({frame(100, net::tcp_syn),
         frame(104, net::tcp_fin | net::tcp_ack, "def"),
         frame(101, net::tcp_ack, "abc")});
  Open();
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "abcdef");
  const auto state = rx_.streams()->state(context.handle);
  ASSERT_TRUE(state);
  EXPECT_EQ(state->reason, net::tcp_finish_reason::fin_observed);
  EXPECT_TRUE(state->stream.end_received);
}

TEST_F(PcapTcpTest, ReportsWindowRejectionAndCanContinueReading) {
  cfg_.streams.stream_capacity = 64;
  Write({frame(100, net::tcp_syn), frame(200, net::tcp_ack, "far"),
         frame(101, net::tcp_ack, "good")});
  Open();
  std::array<char, 8> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -1);
  EXPECT_EQ(rx_.last_result().status,
            net::tcp_manager_insert_status::stream_rejected);
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "good");
  EXPECT_TRUE(rx_.error().empty());
}

TEST_F(PcapTcpTest, DispatchAllowsPartialConsumptionAndExplicitSlotReuse) {
  Write({frame(1, net::tcp_ack, "abc"), frame(4, net::tcp_rst),
         frame(10, net::tcp_syn), frame(11, net::tcp_ack, "next")});
  cfg_.streams.max_connections = 1;
  Open();
  std::optional<net::tcp_connection_handle> handle;
  const auto on_state = [&](const net::tcp_stream_context& context,
                            const net::tcp_managed_stream_state&) {
    handle = context.handle;
  };
  EXPECT_EQ(rx_.dispatch(
                [](const net::tcp_stream_context&, const net::readable_buffers&,
                   net::tcp_stream_status) { return 2U; },
                on_state),
            1);
  ASSERT_TRUE(handle);
  EXPECT_EQ(rx_.last_result().processing.consumed_bytes, 1U);
  std::string suffix;
  const auto consume = [&](const net::tcp_stream_context&,
                           const net::readable_buffers& buffers,
                           net::tcp_stream_status) {
    for (const auto part : {buffers.first, buffers.second}) {
      if (part.size != 0) {
        suffix.append(part.data, part.data + part.size);
      }
    }
    return 0U;
  };
  rx_.streams()->process(*handle, consume, on_state);
  EXPECT_EQ(suffix, "bc");
  EXPECT_EQ(rx_.dispatch(consume, on_state), 1);
  EXPECT_EQ(rx_.streams()->release(*handle).status,
            net::tcp_release_status::released);
  const auto old = *handle;
  EXPECT_EQ(rx_.dispatch(consume, on_state), 1);
  EXPECT_FALSE(*handle == old);
  EXPECT_EQ(rx_.dispatch(consume, on_state), 1);
  EXPECT_EQ(suffix, "bcnext");
}

TEST_F(PcapTcpTest, ChangingConfigurationDoesNotChangeOpenCapture) {
  Write({frame(1, net::tcp_ack, "abc"), frame(4, net::tcp_ack, "def")});
  Open();
  cfg_.rules.clear();
  cfg_.file_path.clear();
  cfg_.verify_checksums = false;
  rx_.set_config(cfg_);
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "abc");
  EXPECT_EQ(Read(context), "def");
  EXPECT_FALSE(rx_.open());
  EXPECT_EQ(rx_.streams(), nullptr);
}

TEST_F(PcapTcpTest, ValidatesLifecycleConfigurationAndReadArguments) {
  std::array<char, 8> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -1);
  EXPECT_FALSE(rx_.open());
  EXPECT_FALSE(rx_.error().empty());
  Write({frame(1, net::tcp_ack, "abc")});
  cfg_.streams.stream_capacity = 63;
  rx_.set_config(cfg_);
  EXPECT_FALSE(rx_.open());
  cfg_.streams.stream_capacity = 128;
  cfg_.rules.push_back({1, kTuple});
  rx_.set_config(cfg_);
  EXPECT_FALSE(rx_.open());
  cfg_.rules.pop_back();
  Open();
  EXPECT_EQ(rx_.read(nullptr, 8), -1);
  EXPECT_EQ(rx_.read(buffer.data(), 0), -1);
  net::tcp_stream_context context;
  EXPECT_EQ(Read(context), "abc");
  Open();
  EXPECT_EQ(Read(context), "abc");
  rx_.close();
  rx_.close();
  EXPECT_EQ(rx_.streams(), nullptr);
}

TEST_F(PcapTcpTest, DeniesAllWithEmptyRulesAndRejectsUnsupportedDatalink) {
  Write({frame(1, net::tcp_ack, "abc")});
  cfg_.rules.clear();
  Open();
  std::array<char, 8> buffer{};
  EXPECT_EQ(rx_.read(buffer.data(), buffer.size()), -2);
  EXPECT_EQ(rx_.streams()->stats().connections, 0U);
  rx_.close();
  Write({frame(1, net::tcp_ack, "abc")}, DLT_NULL);
  rx_.set_config(cfg_);
  EXPECT_FALSE(rx_.open());
  EXPECT_EQ(rx_.streams(), nullptr);
}

TEST_F(PcapTcpTest, DispatchNotifiesControlPacketsAndRejectsReceiverReentry) {
  Write({frame(100, net::tcp_syn), frame(101, net::tcp_ack, "abc"),
         frame(104, net::tcp_rst)});
  Open();
  int data_calls = 0;
  int state_calls = 0;
  net::tcp_finish_reason reason = net::tcp_finish_reason::none;
  const auto on_data = [&](const net::tcp_stream_context&,
                           const net::readable_buffers& buffers,
                           net::tcp_stream_status) {
    ++data_calls;
    EXPECT_EQ(buffers.size(), 3U);
    EXPECT_THROW(rx_.close(), std::logic_error);
    EXPECT_THROW(rx_.open(), std::logic_error);
    return 0U;
  };
  const auto on_state = [&](const net::tcp_stream_context&,
                            const net::tcp_managed_stream_state& state) {
    ++state_calls;
    reason = state.reason;
  };
  EXPECT_EQ(rx_.dispatch(on_data, on_state), 1);
  EXPECT_EQ(rx_.dispatch(on_data, on_state), 1);
  EXPECT_EQ(rx_.dispatch(on_data, on_state), 1);
  EXPECT_EQ(data_calls, 1);
  EXPECT_EQ(state_calls, 3);
  EXPECT_EQ(reason, net::tcp_finish_reason::rst_observed);
}

// NOLINTEND(readability-function-cognitive-complexity)

}  // namespace

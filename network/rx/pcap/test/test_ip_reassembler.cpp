#include "../ip_reassembly.h"

#include <gtest/gtest.h>

#include <array>
#include <netinet/in.h>
#include <string>

namespace {

timeval tv(long seconds) {
  timeval value{};
  value.tv_sec = seconds;
  return value;
}

net::ipv4_fragment_view make_fragment(uint16_t identification, uint16_t field,
                                      const uint8_t *payload, size_t payload_size,
                                      const timeval &timestamp) {
  net::ipv4_fragment_view fragment;
  fragment.key = {1, 2, identification, IPPROTO_UDP};
  fragment.offset_bytes = static_cast<size_t>(field & 0x1fffU) * 8U;
  fragment.more_fragments = (field & 0x2000U) != 0;
  fragment.ip_header_length = 20;
  fragment.payload = payload;
  fragment.payload_size = payload_size;
  fragment.timestamp = timestamp;
  return fragment;
}

net::ip_reassembly_result push_fragment(net::ip_reassembly &reassembler,
                                         uint16_t identification, uint16_t field,
                                         const uint8_t *payload, size_t payload_size,
                                         const timeval &timestamp,
                                         net::reassembled_ip_packet &output) {
  return reassembler.push(make_fragment(identification, field, payload,
                                        payload_size, timestamp), output);
}

}  // namespace

TEST(IpReassembler, ReassemblesOutOfOrderFragments) {
  net::ip_reassembly reassembler;
  reassembler.configure(65535, 3000, 16, 1024);
  net::reassembled_ip_packet output;
  const std::array<uint8_t, 8> second{{'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L'}};
  const std::array<uint8_t, 8> first{{'A', 'B', 'C', 'D', '1', '2', '3', '4'}};

  EXPECT_EQ(push_fragment(reassembler, 7, 0x0001, second.data(), second.size(), tv(1), output),
            net::ip_reassembly_result::incomplete);
  EXPECT_EQ(push_fragment(reassembler, 7, 0x2000, first.data(), first.size(), tv(1), output),
            net::ip_reassembly_result::complete);
  ASSERT_EQ(output.payload_size, 16U);
  EXPECT_EQ(std::string(output.payload, output.payload + output.payload_size), "ABCD1234EFGHIJKL");
}

TEST(IpReassembler, WaitsForGapAfterFirstAndLastFragmentsArrive) {
  net::ip_reassembly reassembler;
  reassembler.configure(65535, 3000, 16, 1024);
  net::reassembled_ip_packet output;
  const std::array<uint8_t, 8> fragment{{0, 1, 2, 3, 4, 5, 6, 7}};

  EXPECT_EQ(push_fragment(reassembler, 8, 0x2000, fragment.data(), fragment.size(), tv(1), output),
            net::ip_reassembly_result::incomplete);
  EXPECT_EQ(push_fragment(reassembler, 8, 0x0002, fragment.data(), fragment.size(), tv(1), output),
            net::ip_reassembly_result::incomplete);
  EXPECT_EQ(push_fragment(reassembler, 8, 0x2001, fragment.data(), fragment.size(), tv(1), output),
            net::ip_reassembly_result::complete);
  EXPECT_EQ(output.payload_size, 24U);
}

TEST(IpReassembler, DoesNotCompleteWithMissingFragment) {
  net::ip_reassembly reassembler;
  reassembler.configure(65535, 3000, 16, 1024);
  net::reassembled_ip_packet output;
  const std::array<uint8_t, 16> first{{0, 1, 2, 3, 4, 5, 6, 7,
                                       8, 9, 10, 11, 12, 13, 14, 15}};

  EXPECT_EQ(push_fragment(reassembler, 7, 0x2000, first.data(), first.size(), tv(1), output),
            net::ip_reassembly_result::incomplete);
}

TEST(IpReassembler, DropsOverlappingFragmentSet) {
  net::ip_reassembly reassembler;
  reassembler.configure(65535, 3000, 16, 1024);
  net::reassembled_ip_packet output;
  const std::array<uint8_t, 16> first{{0, 1, 2, 3, 4, 5, 6, 7,
                                       8, 9, 10, 11, 12, 13, 14, 15}};
  const std::array<uint8_t, 8> overlap{{8, 9, 10, 11, 12, 13, 14, 15}};

  EXPECT_EQ(push_fragment(reassembler, 7, 0x2000, first.data(), first.size(), tv(1), output),
            net::ip_reassembly_result::incomplete);
  EXPECT_EQ(push_fragment(reassembler, 7, 0x0001, overlap.data(), overlap.size(), tv(1), output),
            net::ip_reassembly_result::invalid_fragment);
}

TEST(IpReassembler, EnforcesMaximumPacketSize) {
  net::ip_reassembly reassembler;  // 20-byte IP header + 7-byte payload
  reassembler.configure(27, 3000, 16, 1024);
  net::reassembled_ip_packet output;
  const std::array<uint8_t, 8> fragment{{0, 1, 2, 3, 4, 5, 6, 7}};

  EXPECT_EQ(push_fragment(reassembler, 7, 0x0000, fragment.data(), fragment.size(), tv(1), output),
            net::ip_reassembly_result::invalid_fragment);
}

TEST(IpReassembler, AccountsForIpv4OptionsInPacketLimit) {
  net::ip_reassembly reassembler;
  reassembler.configure(44, 3000, 16, 1024);
  net::reassembled_ip_packet output;
  const std::array<uint8_t, 24> fragment{};
  auto view = make_fragment(7, 0x2000, fragment.data(), fragment.size(), tv(1));
  view.ip_header_length = 24;

  EXPECT_EQ(reassembler.push(view, output), net::ip_reassembly_result::invalid_fragment);
}

TEST(IpReassembler, EnforcesAggregateByteLimitAcrossFragments) {
  net::ip_reassembly reassembler;
  reassembler.configure(65535, 3000, 16, 8);
  net::reassembled_ip_packet output;
  const std::array<uint8_t, 8> fragment{{0, 1, 2, 3, 4, 5, 6, 7}};

  EXPECT_EQ(push_fragment(reassembler, 7, 0x2000, fragment.data(), fragment.size(), tv(1), output),
            net::ip_reassembly_result::incomplete);
  EXPECT_EQ(push_fragment(reassembler, 7, 0x0001, fragment.data(), fragment.size(), tv(1), output),
            net::ip_reassembly_result::resource_limit);
}

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "tcp_four_tuple_filter.h"

namespace {
// GTest assertion branches belong to complete behavioral scenarios.
// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST(TcpFourTupleFilter, RequiresAllFourFieldsAndDoesNotMatchReverse) {
  net::tcp_four_tuple_filter filter;
  const net::tcp_four_tuple tuple{{1, 100}, {2, 200}};
  EXPECT_FALSE(filter.matches(tuple));
  ASSERT_EQ(filter.replace({{1, tuple}}).status,
            net::tcp_filter_update_status::updated);
  EXPECT_TRUE(filter.matches(tuple));
  EXPECT_FALSE(filter.matches({tuple.dst, tuple.src}));
  EXPECT_FALSE(filter.matches({{3, 100}, {2, 200}}));
  EXPECT_FALSE(filter.matches({{1, 100}, {3, 200}}));
  EXPECT_FALSE(filter.matches({{1, 101}, {2, 200}}));
  EXPECT_FALSE(filter.matches({{1, 100}, {2, 201}}));
  filter.replace({{1, tuple}, {2, {tuple.dst, tuple.src}}});
  EXPECT_TRUE(filter.matches(tuple));
  EXPECT_TRUE(filter.matches({tuple.dst, tuple.src}));
}

TEST(TcpFourTupleFilter, EveryZeroFieldIsExactIncludingAnAllZeroTuple) {
  net::tcp_four_tuple_filter filter;
  filter.replace({{1, {{0, 0}, {2, 200}}}, {2, {}}});
  EXPECT_TRUE(filter.matches({{0, 0}, {2, 200}}));
  EXPECT_TRUE(filter.matches({}));
  EXPECT_FALSE(filter.matches({{1, 0}, {2, 200}}));
  EXPECT_FALSE(filter.matches({{0, 1}, {2, 200}}));
  EXPECT_FALSE(filter.matches({{2, 200}, {0, 0}}));
  EXPECT_FALSE(filter.matches({{0, 0}, {0, 1}}));
  filter.replace({});
  EXPECT_FALSE(filter.matches({}));
}

TEST(TcpFourTupleFilter,
     DuplicateIdsRejectAtomicallyAndDuplicateTuplesAreHarmless) {
  net::tcp_four_tuple_filter filter;
  const net::tcp_four_tuple tuple{{1, 100}, {2, 200}};
  ASSERT_EQ(filter.replace({{1, tuple}, {2, tuple}}).generation, 1U);
  const auto rejected = filter.replace({{3, {}}, {3, {}}});
  EXPECT_EQ(rejected.status, net::tcp_filter_update_status::duplicate_rule_id);
  EXPECT_EQ(rejected.generation, 1U);
  EXPECT_EQ(filter.generation(), 1U);
  EXPECT_TRUE(filter.matches(tuple));
  EXPECT_FALSE(filter.matches({}));
}

TEST(TcpFourTupleFilter, HashLookupAgreesWithDirectionalLinearReference) {
  // Deterministic reference tests need reproducible seeds; no security use.
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
  std::mt19937 random(83941);
  net::tcp_four_tuple_filter filter;
  const auto random_tuple = [&]() -> net::tcp_four_tuple {
    return {{static_cast<uint32_t>(random() % 5U),
             static_cast<uint16_t>(random() % 5U)},
            {static_cast<uint32_t>(random() % 5U),
             static_cast<uint16_t>(random() % 5U)}};
  };
  for (size_t round = 0; round != 30; ++round) {
    std::vector<net::tcp_match_rule> rules;
    for (uint64_t rule_id = 0; rule_id != 40; ++rule_id) {
      rules.push_back({rule_id, random_tuple()});
    }
    ASSERT_EQ(filter.replace(rules).status,
              net::tcp_filter_update_status::updated);
    for (size_t query = 0; query != 400; ++query) {
      const auto tuple = random_tuple();
      bool expected = false;
      for (const auto& rule : rules) {
        expected = expected || rule.tuple == tuple;
      }
      ASSERT_EQ(filter.matches(tuple), expected);
    }
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
}  // namespace

#include "../buffer/dynamic_buffer.h"
#include "../buffer/fbuffer.h"
#include "../buffer/fixed_buffer.h"
#include "../container/fstring.h"
#include "../concurrency/async_result_syncer.h"
#include "../container/bheap.h"
#include "../container/fixed_bit_set.h"
#include "../base/raii.h"
#include "../base/scope.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
template <typename Comparator>
void CheckHeapErasure() {
  using Heap = detail::bheap<int, Comparator>;
  for (unsigned seed = 0; seed < 10; ++seed) {
    Heap heap;
    std::multiset<int, Comparator> expected;
    std::vector<typename Heap::bheap_context*> contexts;
    std::mt19937 random(seed);
    for (int i = 0; i < 64; ++i) {
      int value = static_cast<int>(random() % 32);
      expected.insert(value);
      typename Heap::bheap_context* context = nullptr;
      ASSERT_TRUE(heap.insert(std::move(value), context));
      contexts.push_back(context);
    }
    for (int i = 0; i < 32; ++i) {
      const auto entry = random() % contexts.size();
      const auto* context = contexts[entry];
      expected.erase(expected.find(context->val));
      heap.erase(context->idx);
      contexts.erase(contexts.begin() + entry);
      ASSERT_EQ(heap.size(), expected.size());
      ASSERT_EQ(heap.top(), *expected.rbegin());
    }
    for (auto it = expected.rbegin(); it != expected.rend(); ++it) {
      ASSERT_EQ(heap.pop(), *it);
    }
    EXPECT_TRUE(heap.empty());
  }
}

template <std::size_t N>
void CheckBitSearch() {
  for (unsigned pattern = 0; pattern < 256; ++pattern) {
    detail::FixedBitSet<N> bits;
    for (std::size_t pos = 0; pos < N; ++pos) {
      if (pattern & (1u << (pos % 8))) {
        bits.set(pos);
      }
    }
    for (std::size_t offset = 0; offset <= N; ++offset) {
      long long first = -1;
      long long last = -1;
      for (std::size_t pos = 0; pos < N; ++pos) {
        if (pattern & (1u << (pos % 8))) {
          if (pos >= offset && first == -1) {
            first = static_cast<long long>(pos);
          }
          if (pos < N - offset) {
            last = static_cast<long long>(pos);
          }
        }
      }
      ASSERT_EQ(bits.find_first_set(offset), first) << N << ' ' << pattern << ' ' << offset;
      ASSERT_EQ(bits.find_last_set(offset), last) << N << ' ' << pattern << ' ' << offset;
    }
  }
}
}  // namespace

TEST(BasicCorrectness, HeapErasureMatchesOrderedReference) {
  CheckHeapErasure<std::less<int>>();
  CheckHeapErasure<std::greater<int>>();
}

TEST(BasicCorrectness, BitSearchMatchesReferenceAcrossByteBoundaries) {
  CheckBitSearch<1>();
  CheckBitSearch<7>();
  CheckBitSearch<8>();
  CheckBitSearch<9>();
  CheckBitSearch<16>();
  CheckBitSearch<100>();
}

TEST(BasicCorrectness, FixedStringUsesLogicalContentsForConstOperations) {
  const detail::fstring<8> abc("abc"), def("def");
  EXPECT_LT(abc, def);
  EXPECT_GT(def, abc);
  EXPECT_FALSE(abc > def);
  EXPECT_EQ(abc, std::string("abc"));
  EXPECT_EQ(std::string("abc"), abc);
  EXPECT_EQ(abc, detail::fstring<16>("abc"));
  EXPECT_FALSE(static_cast<const char*>(nullptr) == abc);
  detail::fstring<8> same("abc");
  same[7] = 'z';
  EXPECT_EQ(same, abc);
  EXPECT_EQ(abc.find_first_of('b'), 1u);
  EXPECT_EQ(abc.find_first_not_of('a'), 1u);
  EXPECT_EQ(abc.find_first_of('\0'), abc.npos);
  EXPECT_EQ(abc.find_first_of('a', abc.size()), abc.npos);
  EXPECT_EQ(abc.substr(1, std::numeric_limits<std::size_t>::max()), "bc");
  EXPECT_TRUE(abc.substr(abc.size(), 1).empty());
  EXPECT_THROW(abc.substr(8, 1), std::out_of_range);
  EXPECT_EQ(abc.to_string(), "abc");
  EXPECT_EQ(static_cast<std::string>(abc), "abc");
  EXPECT_EQ(static_cast<std::string_view>(abc), "abc");
}

TEST(BasicCorrectness, FixedStringAssignmentAndBothSwapsPreserveValues) {
  detail::fstring<8> a("one"), b("two");
  swap(a, b);
  EXPECT_STREQ(a.c_str(), "two");
  EXPECT_STREQ(b.c_str(), "one");
  a.swap(b);
  EXPECT_STREQ(a.c_str(), "one");
  EXPECT_STREQ(b.c_str(), "two");
  EXPECT_EQ(&(a = b), &a);
  auto& self = a;
  EXPECT_EQ(&(a = self), &a);
  a.swap(a);
  EXPECT_STREQ(a.c_str(), "two");
  detail::fstring<7> odd_a("first"), odd_b("last");
  swap(odd_a, odd_b);
  EXPECT_STREQ(odd_a.c_str(), "last");
  EXPECT_STREQ(odd_b.c_str(), "first");
}

TEST(BasicCorrectness, DynamicBufferCopiesMovesAndBoundsItsReadableRange) {
  detail::DynamicBufferr source;
  source.set_expand_size(4);
  source.append("abcdef", 6);
  source.set_head_size(2);
  detail::DynamicBufferr destination;
  destination.append("old", 3);
  destination = source;
  auto& self = destination;
  destination = self;
  EXPECT_EQ(destination.expand_size(), 4u);
  EXPECT_EQ(destination.size(), 6u);
  EXPECT_EQ(destination.readable_size(), 4u);
  EXPECT_EQ(destination.to_string(), "cdef");
  source[0] = 'X';
  EXPECT_EQ(destination.to_string(), "cdef");
  EXPECT_THROW(destination.set_head_size(7), std::out_of_range);
  detail::DynamicBufferr moved(std::move(destination));
  EXPECT_TRUE(destination.to_string().empty());
  EXPECT_EQ(moved.to_string(), "cdef");
  moved.resize(1);
  EXPECT_EQ(moved.head_size(), 1u);
  EXPECT_TRUE(moved.to_string().empty());
  detail::DynamicBufferr empty;
  moved = empty;
  EXPECT_TRUE(moved.to_string().empty());
}

TEST(BasicCorrectness, DynamicBufferGrowthAndNativeReadHandleBoundaries) {
  detail::DynamicBufferr buffer;
  EXPECT_THROW(buffer.set_expand_size(0), std::invalid_argument);
  buffer.set_expand_size(4);
  buffer.append("1234", 4);
  buffer.append(buffer.data(), buffer.size());
  EXPECT_EQ(buffer.to_string(), "12341234");
  EXPECT_THROW(buffer.reserve(std::numeric_limits<std::size_t>::max()), std::length_error);
  EXPECT_THROW(buffer.append("x", std::numeric_limits<std::size_t>::max()), std::length_error);
  EXPECT_EQ(buffer.to_string(), "12341234");
  buffer.clear();
  std::uint32_t original = 0x13572468;
  buffer.append("x", 1);
  buffer.append(reinterpret_cast<const char*>(&original), sizeof(original));
  buffer.set_head_size(1);
  std::uint32_t result = 0;
  ASSERT_TRUE(buffer.read(result));
  EXPECT_EQ(result, original);
  buffer.set_head_size(2);
  result = 99;
  EXPECT_FALSE(buffer.read(result));
  EXPECT_EQ(result, 99u);
}

TEST(BasicCorrectness, FixedBuffersCompactUnreadBytesAndReuseCapacity) {
  detail::fbuffer<6> buffer;
  ASSERT_TRUE(buffer.append("abcdef", 6));
  ASSERT_NE(buffer.peak(2), nullptr);
  EXPECT_EQ(buffer.peak(std::numeric_limits<std::size_t>::max()), nullptr);
  buffer.shrink_to_fit();
  EXPECT_EQ(std::string(buffer.data(), buffer.size()), "cdef");
  ASSERT_TRUE(buffer.append("gh", 2));
  EXPECT_EQ(std::string(buffer.data(), buffer.size()), "cdefgh");

  detail::FixedBuffer<6> fixed;
  ASSERT_TRUE(fixed.append("abcdef", 6));
  ASSERT_NE(fixed.peek(2), nullptr);
  EXPECT_EQ(fixed.peek(std::numeric_limits<std::size_t>::max()), nullptr);
  fixed.shrink_to_fit();
  EXPECT_EQ(std::string(fixed.data(), fixed.length()), "cdef");
  ASSERT_TRUE(fixed.append("gh", 2));
  ASSERT_NE(fixed.peek(6), nullptr);
  fixed.shrink_to_fit();
  fixed.shrink_to_fit();
  EXPECT_TRUE(fixed.empty());
  EXPECT_EQ(fixed.remaining(), 6u);
}

TEST(BasicCorrectness, RaiiAcquiresAndReleasesExactlyOnce) {
  int acquired = 0;
  int released = 0;
  {
    detail::RAII guard([&] { ++released; }, [&] { ++acquired; });
    EXPECT_EQ(acquired, 1);
    EXPECT_EQ(released, 0);
  }
  EXPECT_EQ(released, 1);
  EXPECT_THROW(
      detail::RAII([&] { ++released; }, [] { throw std::runtime_error("acquire"); }),
      std::runtime_error);
  EXPECT_EQ(released, 1);
  struct Resource {
    int count = 0;
    void acquire() { ++count; }
    void release() { --count; }
  } resource;
  {
    auto guard = detail::make_raii(resource, &Resource::release, &Resource::acquire);
    EXPECT_EQ(resource.count, 1);
  }
  EXPECT_EQ(resource.count, 0);
  {
    detail::RAIIVar<std::unique_ptr<int>> guard(
        [] { return std::make_unique<int>(17); },
        [&](std::unique_ptr<int>& value) { released += *value; });
    auto moved = std::move(guard);
    EXPECT_EQ(**moved, 17);
  }
  EXPECT_EQ(released, 18);
}

TEST(BasicCorrectness, ScopeGuardTransfersCleanupAndSupportsDismissal) {
  int released = 0;
  const auto unwind = [&] {
    auto guard = detail::make_scope([owned = std::make_unique<int>(7), &released] {
      released += *owned;
    });
    auto moved = std::move(guard);
    throw std::runtime_error("unwind");
  };
  EXPECT_THROW(unwind(), std::runtime_error);
  EXPECT_EQ(released, 7);
  {
    auto callback = [&] { ++released; };
    auto guard = detail::make_scope(callback);
    guard.dismiss();
  }
  EXPECT_EQ(released, 7);
}

TEST(BasicCorrectness, LegacySyncerSupportsBlockingAndDeadlineWaits) {
  using Clock = std::chrono::steady_clock;
  using namespace std::chrono_literals;
  auto syncer = detail::make_atrs<int>([](int& value) { return value > 0; });
  const auto first = syncer.fetch_id();
  std::thread notifier([&] { syncer.notify(first, 7); });
  int result = 0;
  const bool ready = syncer.wait(result, first);
  notifier.join();
  EXPECT_TRUE(ready);
  EXPECT_EQ(result, 7);

  const auto second = syncer.fetch_id();
  EXPECT_FALSE(syncer.wait_until(result, second, Clock::now()));
  EXPECT_EQ(result, 7);
  syncer.notify(second, 11);
  EXPECT_TRUE(syncer.wait_until(result, second, Clock::now() + 1s));
  EXPECT_EQ(result, 11);

  const auto third = syncer.fetch_id();
  syncer.notify(third, 13);
  EXPECT_TRUE(syncer.wait_until(third, Clock::now() + 1s));
  const auto fourth = syncer.fetch_id();
  syncer.notify(fourth, -1);
  EXPECT_FALSE(syncer.wait_until(fourth, 1s));
}


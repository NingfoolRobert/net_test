#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <vector>

#include "../sync_pair.h"

namespace {
using namespace std::chrono_literals;

TEST(SyncPairTest, FalseResultIsStillDeliveredAndReclaimed) {
  basic::SyncPair<bool> pair(1);
  const auto id = pair.fetch_id();
  ASSERT_TRUE(pair.prepare(id));
  EXPECT_FALSE(pair.prepare(id));
  ASSERT_TRUE(pair.notify(id, false));
  EXPECT_FALSE(pair.notify(id, true));
  bool value = true;
  EXPECT_TRUE(pair.wait(value, id));
  EXPECT_FALSE(value);
  EXPECT_EQ(pair.size(), 0U);
}

TEST(SyncPairTest, WaitBeforeNotifyAndResetPreservesPreparedState) {
  basic::SyncPair<bool> pair(1);
  ASSERT_TRUE(pair.prepare(1));
  auto waiting = std::async(std::launch::async, [&] {
    bool value = false;
    return pair.wait(value, 1) && value;
  });
  pair.reset();
  EXPECT_TRUE(pair.notify(1, true));
  EXPECT_TRUE(waiting.get());
  EXPECT_EQ(pair.size(), 0U);
}

TEST(SyncPairTest, TimeoutRetainsRecordAndUntilCanConsumeLaterNotification) {
  basic::SyncPair<int> pair(1);
  int value = -1;
  EXPECT_FALSE(pair.wait_for(value, 1, 1ms));
  EXPECT_EQ(value, -1);
  EXPECT_EQ(pair.size(), 1U);
  ASSERT_TRUE(pair.notify(1, 42));
  pair.reset();
  EXPECT_TRUE(pair.wait_until(value, 1, std::chrono::steady_clock::now() + 1s));
  EXPECT_EQ(value, 42);
  EXPECT_EQ(pair.size(), 0U);
}

TEST(SyncPairTest, CancelOnlyUnpublishedWorkAndDoNotDiscardReadyResult) {
  basic::SyncPair<bool> pair(1);
  ASSERT_TRUE(pair.prepare(1));
  EXPECT_TRUE(pair.cancel(1));
  EXPECT_FALSE(pair.cancel(1));
  ASSERT_TRUE(pair.prepare(2));
  ASSERT_TRUE(pair.notify(2, true));
  EXPECT_FALSE(pair.cancel(2));
  bool value = false;
  ASSERT_TRUE(pair.wait(value, 2));
  EXPECT_TRUE(value);
  EXPECT_EQ(pair.size(), 0U);
}

TEST(SyncPairTest, ConcurrentResultsDoNotCrossAndAllRecordsAreReclaimed) {
  basic::SyncPair<int> pair(1);
  constexpr int kProducers = 4;
  constexpr int kEach = 1000;
  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&] {
      for (int i = 0; i < kEach; ++i) {
        const auto id = pair.fetch_id();
        ASSERT_TRUE(pair.prepare(id));
        std::thread notifier(
            [&, id] { EXPECT_TRUE(pair.notify(id, static_cast<int>(id))); });
        int value = -1;
        EXPECT_TRUE(pair.wait(value, id));
        EXPECT_EQ(value, id);
        notifier.join();
      }
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  EXPECT_EQ(pair.size(), 0U);
}

TEST(SyncPairTest, SequenceExhaustionDoesNotWrap) {
  basic::SyncPair<bool> pair(std::numeric_limits<int64_t>::max() - 1);
  EXPECT_EQ(pair.fetch_id(), std::numeric_limits<int64_t>::max() - 1);
  EXPECT_THROW(pair.fetch_id(), std::overflow_error);
  EXPECT_THROW(pair.fetch_id(), std::overflow_error);
}

TEST(SyncPairTest, ResetDoesNotDestroyAnActiveUnpreparedWait) {
  basic::SyncPair<bool> pair(1);
  auto waiting = std::async(std::launch::async, [&] {
    bool value = false;
    return pair.wait(value, 1) && value;
  });
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (pair.size() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  pair.reset();
  EXPECT_TRUE(pair.notify(1, true));
  EXPECT_TRUE(waiting.get());
  EXPECT_EQ(pair.size(), 0U);
}
}  // namespace

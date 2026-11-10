#include "../ring_buffer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <thread>

TEST(RingBuffer, BulkOperationsWrapAndMixWithSingleOperations) {
  RingBuffer<int, 8> queue;
  const int first[] = {1, 2, 3, 4, 5};
  const int second[] = {6, 7, 8, 9};
  int output[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

  EXPECT_TRUE(queue.try_push_bulk(nullptr, 0));
  EXPECT_TRUE(queue.try_pop_bulk(nullptr, 0));
  EXPECT_FALSE(queue.try_push_bulk(first, 9));
  EXPECT_FALSE(queue.try_pop_bulk(output, 9));
  ASSERT_TRUE(queue.try_push_bulk(first, 5));
  EXPECT_FALSE(queue.try_push_bulk(second, 4));
  EXPECT_FALSE(queue.try_pop_bulk(output, 6));
  for (int value : output) {
    EXPECT_EQ(value, -1);
  }

  ASSERT_TRUE(queue.try_pop_bulk(output, 3));
  EXPECT_EQ(output[0], 1);
  EXPECT_EQ(output[1], 2);
  EXPECT_EQ(output[2], 3);
  ASSERT_TRUE(queue.try_push_bulk(second, 4));
  ASSERT_TRUE(queue.try_push(10));

  int single = 0;
  ASSERT_TRUE(queue.try_pop(single));
  EXPECT_EQ(single, 4);
  ASSERT_TRUE(queue.try_pop_bulk(output, 6));
  const int expected[] = {5, 6, 7, 8, 9, 10};
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(output[i], expected[i]);
  }
  EXPECT_TRUE(queue.is_empty());

  const int full[] = {11, 12, 13, 14, 15, 16, 17, 18};
  ASSERT_TRUE(queue.try_push_bulk(full, 8));
  EXPECT_TRUE(queue.is_full());
  EXPECT_FALSE(queue.try_push(19));
  ASSERT_TRUE(queue.try_pop_bulk(output, 8));
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(output[i], full[i]);
  }
  EXPECT_TRUE(queue.is_empty());
}

TEST(RingBuffer, BulkTransfersPreserveOrderAcrossThreads) {
  constexpr int kTotal = 50'003;
  RingBuffer<int, 64> queue;
  std::atomic<bool> producer_done{false};
  std::atomic<int> failures{0};

  std::thread producer([&] {
    int next = 0;
    while (next < kTotal) {
      if (next % 13 == 0) {
        while (!queue.try_push(next)) {
          std::this_thread::yield();
        }
        ++next;
      } else {
        int batch[7];
        const int count = std::min(7, kTotal - next);
        for (int i = 0; i < count; ++i) {
          batch[i] = next + i;
        }
        while (!queue.try_push_bulk(batch, count)) {
          std::this_thread::yield();
        }
        next += count;
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    int expected = 0;
    int output[5];
    while (expected < kTotal) {
      if (queue.try_pop_bulk(output, 5)) {
        for (int value : output) {
          if (value != expected++) {
            failures.fetch_add(1, std::memory_order_relaxed);
          }
        }
      } else if (producer_done.load(std::memory_order_acquire)) {
        int value = 0;
        if (queue.try_pop(value)) {
          if (value != expected++) {
            failures.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          std::this_thread::yield();
        }
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();
  EXPECT_EQ(failures.load(), 0);
  EXPECT_TRUE(queue.is_empty());
}

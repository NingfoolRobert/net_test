#include "../mpmc_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace {

struct Item {
    Item() noexcept = default;
    Item(int producer, int sequence) noexcept
        : producer(producer), sequence(sequence) {}

    Item(const Item &) noexcept = default;
    Item &operator=(const Item &) noexcept = default;
    Item(Item &&) noexcept = default;
    Item &operator=(Item &&) noexcept = default;

    int producer = -1;
    int sequence = -1;
};

struct TrackedItem {
    explicit TrackedItem(int value = 0) noexcept : value(value) {
        live.fetch_add(1, std::memory_order_relaxed);
    }
    TrackedItem(TrackedItem &&other) noexcept : value(other.value) {
        live.fetch_add(1, std::memory_order_relaxed);
    }
    TrackedItem &operator=(TrackedItem &&other) noexcept {
        value = other.value;
        return *this;
    }
    ~TrackedItem() noexcept {
        live.fetch_sub(1, std::memory_order_relaxed);
    }

    int value;
    static std::atomic<int> live;
};

std::atomic<int> TrackedItem::live{0};

}  // namespace

TEST(MpmcQueue, RejectsPushWhenFullAndWraps) {
    MpmcQueue<int, 4> queue;
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_TRUE(queue.try_push(3));
    EXPECT_TRUE(queue.try_push(4));
    EXPECT_TRUE(queue.full());
    EXPECT_FALSE(queue.try_push(5));

    int value = 0;
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.try_push(5));
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 3);
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 4);
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 5);
    EXPECT_FALSE(queue.try_pop(value));
}

TEST(MpmcQueue, DestroysRemainingItemsAfterWraparound) {
    ASSERT_EQ(TrackedItem::live.load(), 0);
    {
        MpmcQueue<TrackedItem, 4> queue;
        for (int value = 1; value <= 4; ++value) {
            ASSERT_TRUE(queue.try_emplace(value));
        }
        TrackedItem output;
        ASSERT_TRUE(queue.try_pop(output));
        ASSERT_EQ(output.value, 1);
        ASSERT_TRUE(queue.try_pop(output));
        ASSERT_EQ(output.value, 2);
        ASSERT_TRUE(queue.try_emplace(5));
        ASSERT_TRUE(queue.try_emplace(6));
        EXPECT_EQ(TrackedItem::live.load(), 5);
    }
    EXPECT_EQ(TrackedItem::live.load(), 0);
}

TEST(MpmcQueue, BulkOperationsHandleWraparoundAndFailures) {
    MpmcQueue<int, 8> queue;
    const int first[] = {1, 2, 3, 4, 5};
    const int second[] = {6, 7, 8, 9};
    int output[6] = {-1, -1, -1, -1, -1, -1};

    EXPECT_TRUE(queue.try_push_bulk(nullptr, 0));
    EXPECT_TRUE(queue.try_pop_bulk(nullptr, 0));
    EXPECT_FALSE(queue.try_push_bulk(first, 9));
    EXPECT_FALSE(queue.try_pop_bulk(output, 9));
    ASSERT_TRUE(queue.try_push_bulk(first, 5));
    EXPECT_FALSE(queue.try_push_bulk(second, 4));
    EXPECT_FALSE(queue.try_pop_bulk(output, 6));
    EXPECT_EQ(output[0], -1);

    ASSERT_TRUE(queue.try_pop_bulk(output, 3));
    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 3);
    ASSERT_TRUE(queue.try_push_bulk(second, 4));
    ASSERT_TRUE(queue.try_pop_bulk(output, 6));
    const int expected[] = {4, 5, 6, 7, 8, 9};
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(output[i], expected[i]);
    }
    EXPECT_TRUE(queue.empty());
}

TEST(MpmcQueue, BulkOperationsDeliverEachItemExactlyOnce) {
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kItemsPerProducer = 2048;
    constexpr int kTotal = kProducers * kItemsPerProducer;
    MpmcQueue<int, 64> queue;
    std::vector<std::atomic<int>> seen(kTotal);
    for (auto &entry : seen) {
        entry.store(0, std::memory_order_relaxed);
    }
    std::atomic<int> producers_done{0};
    std::atomic<int> invalid{0};
    std::vector<int> consumed(kConsumers, 0);
    std::vector<std::thread> threads;

    for (int consumer = 0; consumer < kConsumers; ++consumer) {
        threads.emplace_back([&, consumer] {
            int values[8];
            for (;;) {
                int count = 0;
                if (queue.try_pop_bulk(values, 8)) {
                    count = 8;
                } else if (producers_done.load(std::memory_order_acquire) ==
                           kProducers) {
                    if (!queue.try_pop(values[0])) {
                        break;
                    }
                    count = 1;
                } else {
                    std::this_thread::yield();
                }
                for (int i = 0; i < count; ++i) {
                    if (values[i] < 0 || values[i] >= kTotal) {
                        invalid.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        seen[values[i]].fetch_add(1,
                                                  std::memory_order_relaxed);
                    }
                }
                consumed[consumer] += count;
            }
        });
    }
    for (int producer = 0; producer < kProducers; ++producer) {
        threads.emplace_back([&, producer] {
            for (int item = 0; item < kItemsPerProducer; item += 8) {
                int values[8];
                for (int i = 0; i < 8; ++i) {
                    values[i] = producer * kItemsPerProducer + item + i;
                }
                while (!queue.try_push_bulk(values, 8)) {
                    std::this_thread::yield();
                }
            }
            producers_done.fetch_add(1, std::memory_order_release);
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    int total_consumed = 0;
    for (int count : consumed) {
        total_consumed += count;
    }
    EXPECT_EQ(total_consumed, kTotal);
    EXPECT_EQ(invalid.load(), 0);
    for (const auto &entry : seen) {
        EXPECT_EQ(entry.load(std::memory_order_relaxed), 1);
    }
}

TEST(MpmcQueue, PreservesAllItemsWithMultipleProducersAndConsumers) {
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kItemsPerProducer = 5000;
    constexpr int kTotalItems = kProducers * kItemsPerProducer;

    MpmcQueue<Item, 1024> queue;
    std::atomic<bool> start{false};
    std::atomic<int> ready{0};
    std::atomic<int> consumed{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kProducers + kConsumers);

    for (int producer = 0; producer < kProducers; ++producer) {
        threads.emplace_back([&, producer] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int sequence = 0; sequence < kItemsPerProducer; ++sequence) {
                while (!queue.try_emplace(producer, sequence)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int consumer = 0; consumer < kConsumers; ++consumer) {
        threads.emplace_back([&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            Item item;
            for (;;) {
                const int before = consumed.load(std::memory_order_acquire);
                if (before >= kTotalItems) {
                    return;
                }
                if (!queue.try_pop(item)) {
                    std::this_thread::yield();
                    continue;
                }
                if (item.producer < 0 || item.producer >= kProducers ||
                    item.sequence < 0 || item.sequence >= kItemsPerProducer) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
                consumed.fetch_add(1, std::memory_order_release);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != kProducers + kConsumers) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(consumed.load(), kTotalItems);
    EXPECT_EQ(failures.load(), 0);
    EXPECT_TRUE(queue.empty());
}

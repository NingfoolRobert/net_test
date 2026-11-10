#include "../mpsc_queue.h"

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

}  // namespace

TEST(MpscQueue, RejectsPushWhenFullAndWraps) {
    MpscQueue<int, 4> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());

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

TEST(MpscQueue, PreservesAllItemsWithMultipleProducers) {
    constexpr int kProducers = 4;
    constexpr int kItemsPerProducer = 10000;
    constexpr int kTotalItems = kProducers * kItemsPerProducer;

    MpscQueue<Item, 1024> queue;
    std::atomic<bool> start{false};
    std::atomic<int> producers_ready{0};
    std::atomic<int> consumed{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducers);

    for (int producer = 0; producer < kProducers; ++producer) {
        producers.emplace_back([&, producer] {
            producers_ready.fetch_add(1, std::memory_order_release);
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

    while (producers_ready.load(std::memory_order_acquire) != kProducers) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    std::vector<int> next_sequence(kProducers, 0);
    while (consumed.load(std::memory_order_relaxed) < kTotalItems) {
        Item item;
        if (!queue.try_pop(item)) {
            std::this_thread::yield();
            continue;
        }

        if (item.producer < 0 || item.producer >= kProducers ||
            item.sequence != next_sequence[item.producer]) {
            failures.fetch_add(1, std::memory_order_relaxed);
        } else {
            ++next_sequence[item.producer];
        }
        consumed.fetch_add(1, std::memory_order_relaxed);
    }

    for (auto &producer : producers) {
        producer.join();
    }

    EXPECT_EQ(failures.load(), 0);
    EXPECT_TRUE(queue.empty());
    for (int sequence : next_sequence) {
        EXPECT_EQ(sequence, kItemsPerProducer);
    }
}

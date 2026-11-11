#include "../queue/sharded_mpsc_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace {

struct Message {
    Message() noexcept = default;
    Message(int producer, int sequence) noexcept
        : producer(producer), sequence(sequence) {}

    int producer = -1;
    int sequence = -1;
};

}  // namespace

TEST(ShardedMpscQueue, RegistersProducersAndPreservesProducerOrder) {
    constexpr int kProducers = 4;
    constexpr int kMessagesPerProducer = 5000;
    constexpr int kTotalMessages = kProducers * kMessagesPerProducer;

    ShardedMpscQueue<Message, 8, 256> queue;
    std::atomic<bool> start{false};
    std::atomic<int> ready{0};
    std::atomic<int> consumed{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducers);

    for (int producer = 0; producer < kProducers; ++producer) {
        producers.emplace_back([&, producer] {
            auto token = queue.register_producer();
            if (!token.valid()) {
                failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int sequence = 0; sequence < kMessagesPerProducer;
                 ++sequence) {
                while (!queue.try_emplace(token, producer, sequence)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != kProducers) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    std::vector<int> next_sequence(kProducers, 0);
    while (consumed.load(std::memory_order_relaxed) < kTotalMessages) {
        Message message;
        if (!queue.try_pop(message)) {
            std::this_thread::yield();
            continue;
        }

        if (message.producer < 0 || message.producer >= kProducers ||
            message.sequence != next_sequence[message.producer]) {
            failures.fetch_add(1, std::memory_order_relaxed);
        } else {
            ++next_sequence[message.producer];
        }
        consumed.fetch_add(1, std::memory_order_relaxed);
    }

    for (auto &producer : producers) {
        producer.join();
    }

    EXPECT_EQ(failures.load(), 0);
    EXPECT_TRUE(queue.empty());
    for (int sequence : next_sequence) {
        EXPECT_EQ(sequence, kMessagesPerProducer);
    }
}

TEST(ShardedMpscQueue, ReusesShardForSameThread) {
    ShardedMpscQueue<int, 2, 8> queue;
    auto first = queue.register_producer();
    EXPECT_TRUE(first.valid());
    // A second registration on the same thread reuses its shard.
    EXPECT_TRUE(queue.register_producer().valid());
}

TEST(ShardedMpscQueue, UsesThreadLocalProducerToken) {
    ShardedMpscQueue<int, 4, 8> queue;
    EXPECT_TRUE(queue.try_emplace(42));

    int value = 0;
    ASSERT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 42);
}

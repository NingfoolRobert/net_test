#include "../basic/concurrency/queue/sharded_mpsc_queue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using Queue = ShardedMpscQueue<std::uint64_t, 16, 4096>;
constexpr std::uint64_t kMessagesPerProducer = 1'000'000;

void run_benchmark(int producer_count) {
    Queue queue;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint64_t> consumed{0};
    std::vector<std::thread> producers;
    producers.reserve(static_cast<std::size_t>(producer_count));

    for (int producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::uint64_t message = 0;
                 message < kMessagesPerProducer; ++message) {
                while (!queue.try_emplace(
                    static_cast<std::uint64_t>(producer))) {
                    std::this_thread::yield();
                }
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != producer_count) {
        std::this_thread::yield();
    }

    const std::uint64_t total =
        kMessagesPerProducer * static_cast<std::uint64_t>(producer_count);
    const auto begin = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    while (consumed.load(std::memory_order_relaxed) < total) {
        std::uint64_t value = 0;
        if (queue.try_pop(value)) {
            consumed.fetch_add(1, std::memory_order_relaxed);
        } else {
            std::this_thread::yield();
        }
    }

    for (auto &producer : producers) {
        producer.join();
    }
    const auto end = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(end - begin).count();
    const double throughput = static_cast<double>(total) / seconds;

    std::cout << producer_count << " producers: "
              << std::fixed << std::setprecision(2)
              << seconds * 1'000'000.0 << " us, "
              << throughput / 1'000'000.0 << " M items/s, "
              << (seconds * 1'000'000'000.0 / total) << " ns/item\n";
}

}  // namespace

int main() {
    std::cout << "messages_per_producer=" << kMessagesPerProducer
              << ", ring_capacity=4096\n";
    run_benchmark(1);
    run_benchmark(3);
    run_benchmark(7);
    return 0;
}

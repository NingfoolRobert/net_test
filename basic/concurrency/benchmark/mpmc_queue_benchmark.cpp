#include "../queue/mpmc_queue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kCapacity = 4096;
constexpr std::uint64_t kDefaultItemsPerProducer = 2'000'000;
constexpr std::size_t kLatencySamples = 50'000;
constexpr int kTrials = 5;

void spin_pause() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

struct alignas(64) ConsumerResult {
    std::uint64_t count = 0;
    std::uint64_t sum = 0;
};

struct ThroughputResult {
    double seconds = 0;
    std::uint64_t count = 0;
    std::uint64_t sum = 0;
};

template <std::size_t Batch>
ThroughputResult run_throughput(int producer_count,
                                int consumer_count,
                                std::uint64_t items_per_producer) {
    MpmcQueue<std::uint64_t, kCapacity> queue;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> producers_done{0};
    std::vector<ConsumerResult> results(
        static_cast<std::size_t>(consumer_count));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(producer_count + consumer_count));

    for (int consumer = 0; consumer < consumer_count; ++consumer) {
        threads.emplace_back([&, consumer] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                spin_pause();
            }

            ConsumerResult &result = results[static_cast<std::size_t>(consumer)];
            std::uint64_t value = 0;
            std::uint64_t batch[Batch];
            for (;;) {
                if constexpr (Batch == 1) {
                    if (queue.try_pop(value)) {
                        ++result.count;
                        result.sum += value;
                        continue;
                    }
                } else {
                    if (queue.try_pop_bulk(batch, Batch)) {
                        result.count += Batch;
                        for (std::uint64_t item : batch) {
                            result.sum += item;
                        }
                        continue;
                    }
                }
                if (producers_done.load(std::memory_order_acquire) ==
                    producer_count) {
                    // Drain a partial final batch, or a batch split by
                    // another consumer, before deciding the queue is empty.
                    if constexpr (Batch > 1) {
                        if (queue.try_pop(value)) {
                            ++result.count;
                            result.sum += value;
                            continue;
                        }
                    }
                    break;
                }
                spin_pause();
            }
        });
    }

    for (int producer = 0; producer < producer_count; ++producer) {
        threads.emplace_back([&, producer] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                spin_pause();
            }

            const std::uint64_t first =
                static_cast<std::uint64_t>(producer) * items_per_producer;
            for (std::uint64_t item = 0; item < items_per_producer;
                 item += Batch) {
                if constexpr (Batch == 1) {
                    while (!queue.try_push(first + item)) {
                        spin_pause();
                    }
                } else {
                    std::uint64_t batch[Batch];
                    const std::size_t count = static_cast<std::size_t>(
                        std::min<std::uint64_t>(Batch,
                            items_per_producer - item));
                    for (std::size_t offset = 0; offset < count; ++offset) {
                        batch[offset] = first + item + offset;
                    }
                    while (!queue.try_push_bulk(batch, count)) {
                        spin_pause();
                    }
                }
            }
            producers_done.fetch_add(1, std::memory_order_release);
        });
    }

    while (ready.load(std::memory_order_acquire) !=
           producer_count + consumer_count) {
        spin_pause();
    }
    const auto begin = Clock::now();
    start.store(true, std::memory_order_release);

    for (std::thread &thread : threads) {
        thread.join();
    }
    const auto end = Clock::now();

    ThroughputResult result;
    result.seconds = std::chrono::duration<double>(end - begin).count();
    for (const ConsumerResult &consumer : results) {
        result.count += consumer.count;
        result.sum += consumer.sum;
    }
    return result;
}

struct LatencyItem {
    std::int64_t sent_ns = 0;
};

std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch())
        .count();
}

std::vector<std::int64_t> run_handoff_latency() {
    MpmcQueue<LatencyItem, kCapacity> queue;
    std::vector<std::int64_t> samples(kLatencySamples);
    std::atomic<bool> ready{false};
    std::atomic<std::size_t> acknowledged{0};

    std::thread consumer([&] {
        ready.store(true, std::memory_order_release);
        for (std::size_t index = 0; index < kLatencySamples; ++index) {
            LatencyItem item;
            while (!queue.try_pop(item)) {
                spin_pause();
            }
            samples[index] = now_ns() - item.sent_ns;
            acknowledged.store(index + 1, std::memory_order_release);
        }
    });

    while (!ready.load(std::memory_order_acquire)) {
        spin_pause();
    }
    for (std::size_t index = 0; index < kLatencySamples; ++index) {
        const LatencyItem item{now_ns()};
        while (!queue.try_push(item)) {
            spin_pause();
        }
        while (acknowledged.load(std::memory_order_acquire) != index + 1) {
            spin_pause();
        }
    }
    consumer.join();
    std::sort(samples.begin(), samples.end());
    return samples;
}

}  // namespace

int main(int argc, char **argv) {
    const std::uint64_t items_per_producer = argc > 1
        ? std::strtoull(argv[1], nullptr, 10)
        : kDefaultItemsPerProducer;
    // Keep the expected sum within uint64_t for the 4P4C case.
    if (items_per_producer == 0 || items_per_producer > 1'000'000'000ULL) {
        std::cerr << "invalid items_per_producer\n";
        return 1;
    }

    std::cout << "capacity=" << kCapacity
              << ", items_per_producer=" << items_per_producer
              << ", trials=" << kTrials << '\n';

    for (int batch_size : {1, 16}) {
        for (int threads : {1, 2, 4}) {
            const std::uint64_t total =
                static_cast<std::uint64_t>(threads) * items_per_producer;
            const std::uint64_t expected_sum = total * (total - 1) / 2;
            std::vector<double> throughput;
            throughput.reserve(kTrials);
            for (int trial = 0; trial < kTrials; ++trial) {
                const ThroughputResult result = batch_size == 1
                    ? run_throughput<1>(threads, threads, items_per_producer)
                    : run_throughput<16>(threads, threads, items_per_producer);
                if (result.count != total || result.sum != expected_sum) {
                    std::cerr << "validation failed: batch=" << batch_size
                              << ", " << threads << "P" << threads
                              << "C, count=" << result.count
                              << ", sum=" << result.sum << '\n';
                    return 1;
                }
                throughput.push_back(static_cast<double>(total) /
                                     result.seconds / 1'000'000.0);
            }
            std::sort(throughput.begin(), throughput.end());
            std::cout << "batch=" << batch_size << " " << threads << "P"
                      << threads << "C: "
                      << std::fixed << std::setprecision(2)
                      << "median=" << throughput[kTrials / 2] << " M items/s"
                      << ", range=" << throughput.front() << "-"
                      << throughput.back() << " M items/s"
                      << ", amortized=" << 1000.0 / throughput[kTrials / 2]
                      << " ns/item [OK]\n";
        }
    }

    const std::vector<std::int64_t> latency = run_handoff_latency();
    std::cout << "1P1C handoff (one item in flight, "
              << kLatencySamples << " samples): p50="
              << latency[kLatencySamples / 2] << " ns, p99="
              << latency[kLatencySamples * 99 / 100] << " ns"
              << ", max=" << latency.back() << " ns\n";
    return 0;
}

#include "../basic/concurrency/queue/ring_buffer.h"

#include "../basic/concurrency/queue/lock_free_array.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>
#include <utility>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace {

constexpr std::size_t kCapacity = 1024;
constexpr std::uint64_t kDefaultOperations = 10'000'000;

inline void spin_pause() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

struct BenchmarkResult {
    double milliseconds = 0.0;
    std::uint64_t checksum = 0;
};

template <typename Queue, typename Push, typename Pop>
BenchmarkResult run_spsc(Queue &queue,
                         std::uint64_t operations,
                         Push push,
                         Pop pop) {
    std::atomic<bool> producer_ready{false};
    std::atomic<bool> consumer_ready{false};
    std::atomic<bool> start{false};
    std::uint64_t checksum = 0;

    std::thread producer([&] {
        producer_ready.store(true, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            spin_pause();
        }

        for (std::uint64_t value = 0; value < operations; ++value) {
            while (!push(queue, static_cast<int>(value))) {
                spin_pause();
            }
        }
    });

    std::thread consumer([&] {
        consumer_ready.store(true, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            spin_pause();
        }

        int value = 0;
        for (std::uint64_t count = 0; count < operations; ++count) {
            while (!pop(queue, value)) {
                spin_pause();
            }
            checksum += static_cast<std::uint64_t>(value);
        }
    });

    while (!producer_ready.load(std::memory_order_acquire) ||
           !consumer_ready.load(std::memory_order_acquire)) {
        spin_pause();
    }

    const auto begin = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    const auto end = std::chrono::steady_clock::now();
    const double milliseconds =
        std::chrono::duration<double, std::milli>(end - begin).count();

    return {milliseconds, checksum};
}

void print_result(const char *name,
                  const BenchmarkResult &result,
                  std::uint64_t operations) {
    const double seconds = result.milliseconds / 1000.0;
    const double operations_per_second =
        seconds == 0.0 ? 0.0 : operations / seconds;
    const std::uint64_t expected_checksum =
        operations * (operations - 1) / 2;

    std::cout << std::left << std::setw(18) << name
              << " time=" << std::fixed << std::setprecision(3)
              << result.milliseconds << " ms"
              << " throughput=" << std::setprecision(2)
              << operations_per_second / 1'000'000.0 << " M/s"
              << " checksum=" << result.checksum
              << (result.checksum == expected_checksum ? " [OK]" :
                                                          " [ERROR]")
              << '\n';
}

}  // namespace

int main(int argc, char **argv) {
    const std::uint64_t operations = argc > 1
                                         ? std::strtoull(argv[1], nullptr, 10)
                                         : kDefaultOperations;

    std::cout << "operations=" << operations
              << ", capacity=" << kCapacity << '\n';

    RingBuffer<int, kCapacity> ring_buffer;
    const auto ring_result = run_spsc(
        ring_buffer,
        operations,
        [](auto &queue, int value) {
            return queue.try_push(std::move(value));
        },
        [](auto &queue, int &value) {
            return queue.try_pop(value);
        });

    lock_free_array<int, kCapacity> lock_free;
    const auto lock_free_result = run_spsc(
        lock_free,
        operations,
        [](auto &queue, int value) {
            return queue.push(value);
        },
        [](auto &queue, int &value) {
            return queue.try_pop(value);
        });

    print_result("RingBuffer", ring_result, operations);
    print_result("lock_free_array", lock_free_result, operations);
    return ring_result.checksum == operations * (operations - 1) / 2 &&
                   lock_free_result.checksum ==
                       operations * (operations - 1) / 2
               ? 0
               : 1;
}

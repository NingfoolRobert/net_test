#include "object_pool.h"

#include <benchmark/benchmark.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

struct BenchmarkObject {
    explicit BenchmarkObject(int value) noexcept : value(value) {}

    int value;
};

using Pool = object_pool<BenchmarkObject, 1024, 2048>;

static void BM_ObjectPoolAcquireRelease(benchmark::State &state) {
    Pool pool;
    pool.create_batch(1024, 7);

    for (auto _ : state) {
        BenchmarkObject *value = nullptr;
        benchmark::DoNotOptimize(pool.try_acquire(value));
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
        benchmark::DoNotOptimize(pool.try_release(value));
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_ObjectPoolMpscAcquireRelease(benchmark::State &state) {
    Pool pool;
    pool.create_batch(1024, 7);
    const int worker_count = static_cast<int>(state.range(0));
    std::array<std::atomic<BenchmarkObject *>, 4> tasks{};
    for (auto &task : tasks) {
        task.store(nullptr, std::memory_order_relaxed);
    }
    std::atomic<bool> stop{false};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back([&, i] {
            for (;;) {
                BenchmarkObject *value =
                    tasks[i].exchange(nullptr, std::memory_order_acquire);
                if (value != nullptr) {
                    if (!pool.try_release(value)) {
                        std::abort();
                    }
                } else if (stop.load(std::memory_order_acquire) &&
                           tasks[i].load(std::memory_order_relaxed) == nullptr) {
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    int worker = 0;
    for (auto _ : state) {
        while (tasks[worker].load(std::memory_order_relaxed) != nullptr) {
            std::this_thread::yield();
        }
        BenchmarkObject *value = nullptr;
        while (!pool.try_acquire(value)) {
            std::this_thread::yield();
        }
        benchmark::DoNotOptimize(value->value);
        tasks[worker].store(value, std::memory_order_release);
        worker = (worker + 1) % worker_count;
    }

    stop.store(true, std::memory_order_release);
    for (auto &thread : workers) {
        thread.join();
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ObjectPoolAcquireRelease)->UseRealTime();
BENCHMARK(BM_ObjectPoolMpscAcquireRelease)
    ->Arg(1)
    ->Arg(4)
    ->UseRealTime();

}  // namespace

BENCHMARK_MAIN();

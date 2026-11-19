#include "../basic/concurrency/queue/mpsc_queue.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <thread>

namespace {

MpscQueue<int, 4096> &queue() {
    static MpscQueue<int, 4096> value;
    return value;
}

void BM_MpscQueue(benchmark::State &state) {
    MpscQueue<int, 4096> &shared_queue = queue();

    if (state.thread_index() == 0) {
        // One consumer drains one item from each of the other three threads.
        for (auto _ : state) {
            int value = 0;
            for (int producer = 1; producer < state.threads(); ++producer) {
                while (!shared_queue.try_pop(value)) {
                    std::this_thread::yield();
                }
                benchmark::DoNotOptimize(value);
            }
        }
        state.SetItemsProcessed(state.iterations() *
                                static_cast<std::uint64_t>(state.threads() - 1));
        return;
    }

    for (auto _ : state) {
        while (!shared_queue.try_emplace(state.thread_index())) {
            std::this_thread::yield();
        }
    }
}

BENCHMARK(BM_MpscQueue)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

}  // namespace

BENCHMARK_MAIN();

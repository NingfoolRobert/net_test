#include "../../core/object_pool.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace {

struct TestObject {
    explicit TestObject(int value) noexcept : value(value) {}

    int value;
    int id = -1;
    int generation = 0;
};

}  // namespace

TEST(ObjectPool, CreatesInBatchesAndExpandsAtLowWatermark) {
    object_pool<TestObject, 4, 8> pool;

    EXPECT_EQ(pool.create_batch(4, 1), 4U);
    EXPECT_EQ(pool.capacity(), 4U);
    EXPECT_EQ(pool.available(), 4U);

    EXPECT_EQ(pool.expand_if_below_low_watermark(5, 4, 2), 4U);
    EXPECT_EQ(pool.capacity(), 8U);
    EXPECT_EQ(pool.available(), 8U);

    std::array<TestObject *, 6> values{};
    EXPECT_EQ(pool.try_acquire_batch(values.data(), values.size()), 6U);
    EXPECT_EQ(pool.available(), 2U);
    for (TestObject *value : values) {
        ASSERT_NE(value, nullptr);
    }

    EXPECT_EQ(pool.try_release_batch(values.data(), values.size()), 6U);
    EXPECT_EQ(pool.available(), 8U);
    EXPECT_EQ(pool.in_use(), 0U);
}

TEST(ObjectPool, RejectsDoubleRelease) {
    object_pool<TestObject, 4, 8> pool;
    ASSERT_EQ(pool.create_batch(4, 7), 4U);

    TestObject *value = nullptr;
    ASSERT_TRUE(pool.try_acquire(value));
    ASSERT_TRUE(pool.try_release(value));
    EXPECT_FALSE(pool.try_release(value));
}

TEST(ObjectPool, SupportsSingleAcquirerAndMultipleReleasers) {
    constexpr int kObjects = 64;
    constexpr int kWorkers = 4;
    constexpr int kOperationsPerWorker = 5000;
    object_pool<TestObject, 16, 32> pool;
    ASSERT_EQ(pool.create_batch(kObjects, 9), static_cast<std::size_t>(kObjects));

    std::array<TestObject *, kObjects> values{};
    ASSERT_EQ(pool.try_acquire_batch(values.data(), values.size()),
              values.size());
    for (int i = 0; i < kObjects; ++i) {
        values[i]->id = i;
    }
    ASSERT_EQ(pool.try_release_batch(values.data(), values.size()),
              values.size());

    std::array<std::atomic<TestObject *>, kWorkers> tasks{};
    for (auto &task : tasks) {
        task.store(nullptr, std::memory_order_relaxed);
    }
    std::array<int, kObjects> generations{};
    std::atomic<int> workers_ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);

    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&, i] {
            workers_ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int operation = 0; operation < kOperationsPerWorker;
                 ++operation) {
                TestObject *value = nullptr;
                while ((value = tasks[i].exchange(
                            nullptr, std::memory_order_acquire)) == nullptr) {
                    std::this_thread::yield();
                }
                ++value->generation;
                if (value->value != 9 || !pool.try_release(value)) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    while (workers_ready.load(std::memory_order_acquire) != kWorkers) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    // The test thread is the only pool consumer; workers only return objects.
    for (int operation = 0; operation < kWorkers * kOperationsPerWorker;
         ++operation) {
        const int worker = operation % kWorkers;
        while (tasks[worker].load(std::memory_order_relaxed) != nullptr) {
            std::this_thread::yield();
        }
        TestObject *value = nullptr;
        while (!pool.try_acquire(value)) {
            std::this_thread::yield();
        }
        if (value->id < 0 || value->id >= kObjects) {
            failures.fetch_add(1, std::memory_order_relaxed);
        } else {
            if (value->generation != generations[value->id]) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
            ++generations[value->id];
        }
        tasks[worker].store(value, std::memory_order_release);
    }

    for (auto &worker : workers) {
        worker.join();
    }

    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(pool.available(), static_cast<std::size_t>(kObjects));
    EXPECT_EQ(pool.in_use(), 0U);
    ASSERT_EQ(pool.try_acquire_batch(values.data(), values.size()),
              values.size());
    std::array<bool, kObjects> seen{};
    for (TestObject *value : values) {
        ASSERT_GE(value->id, 0);
        ASSERT_LT(value->id, kObjects);
        EXPECT_FALSE(seen[value->id]);
        seen[value->id] = true;
        EXPECT_EQ(value->generation, generations[value->id]);
    }
    EXPECT_EQ(pool.try_release_batch(values.data(), values.size()),
              values.size());
}

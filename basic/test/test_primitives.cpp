#include "../base/auto_ptr.h"
#include "../buffer/buffer.h"
#include "../memory/fixed_memory_pool.h"
#include "../container/fixed_vector.h"
#include "../memory/memory_pool.h"
#include "../buffer/mutable_buffer.h"
#include "../memory/object_pool.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <thread>
#include <vector>

TEST(OwnedBuffer, GrowsShrinksCopiesAndMoves) {
    detail::buffer_t buffer;
    EXPECT_TRUE(buffer.append(nullptr, 0));
    EXPECT_FALSE(buffer.append("abc", 3));
    EXPECT_EQ(buffer.resize(8), 8U);
    EXPECT_TRUE(buffer.append("abc", 3));
    EXPECT_TRUE(buffer.append(buffer.data(), 3));
    EXPECT_EQ(buffer.data_size(), 6U);
    EXPECT_EQ(std::string(buffer.data(), 6), "abcabc");
    detail::buffer_t copy = buffer;
    buffer.buf()[0] = 'x';
    EXPECT_EQ(copy.data()[0], 'a');
    EXPECT_EQ(copy.resize(3), 3U);
    EXPECT_EQ(copy.size(), 3U);
    EXPECT_EQ(copy.resize(12), 12U);
    EXPECT_TRUE(copy.append("def", 3));
    detail::buffer_t moved = std::move(copy);
    EXPECT_EQ(copy.capacity(), 0U);
    EXPECT_EQ(copy.data(), nullptr);
    EXPECT_EQ(std::string(moved.data(), moved.data_size()), "abcdef");
    moved.resize(0);
    EXPECT_EQ(moved.data(), nullptr);
    EXPECT_EQ(moved.size(), 0U);
}
TEST(OwnedBuffer, CopiesTrivialObjectsAndRejectsOverflow) {
    detail::buffer_t buffer(sizeof(uint32_t));
    const uint32_t value = 42;
    EXPECT_TRUE(buffer.copy(value));
    uint32_t read = 0;
    std::memcpy(&read, buffer.data(), sizeof(read));
    EXPECT_EQ(read, value);
    EXPECT_FALSE(buffer.append("x", 1));
    buffer.clear();
    EXPECT_FALSE(buffer.append(nullptr, 1));
    EXPECT_EQ(buffer.size(), 0U);
}
TEST(OwnedBuffer, AlignsAndZeroInitializesAllocatedStorage) {
    detail::buffer_t buffer(7);
    ASSERT_NE(buffer.buf(), nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffer.buf()) % 64, 0U);
    for (unsigned int i = 0; i < buffer.capacity(); ++i) {
        EXPECT_EQ(buffer.buf()[i], '\0');
    }
    ASSERT_TRUE(buffer.append("abc", 3));
    detail::buffer_t copy(buffer);
    ASSERT_NE(copy.buf(), nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(copy.buf()) % 64, 0U);
    EXPECT_EQ(std::string(copy.data(), copy.size()), "abc");
    for (unsigned int i = copy.size(); i < copy.capacity(); ++i) {
        EXPECT_EQ(copy.buf()[i], '\0');
    }
    EXPECT_EQ(buffer.resize(101), 101U);
    ASSERT_NE(buffer.buf(), nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffer.buf()) % 64, 0U);
    EXPECT_EQ(std::string(buffer.data(), buffer.size()), "abc");
    for (unsigned int i = buffer.size(); i < buffer.capacity(); ++i) {
        EXPECT_EQ(buffer.buf()[i], '\0');
    }
    EXPECT_EQ(buffer.resize(0), 0U);
    EXPECT_EQ(buffer.buf(), nullptr);
    detail::buffer_t empty_copy(buffer);
    EXPECT_EQ(empty_copy.buf(), nullptr);
    EXPECT_EQ(empty_copy.capacity(), 0U);
}
TEST(OwnedBuffer, ResetOnlyGrowsAndPreservesData) {
    detail::buffer_t buffer(8);
    ASSERT_TRUE(buffer.append("abc", 3));
    const auto original_address = reinterpret_cast<std::uintptr_t>(buffer.buf());
    for (unsigned int capacity : {0U, 1U, 3U, 7U, 8U}) {
        buffer.reset(capacity);
        EXPECT_EQ(buffer.capacity(), 8U);
        EXPECT_EQ(buffer.size(), 3U);
        EXPECT_EQ(buffer.data_size(), 3U);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffer.buf()), original_address);
        EXPECT_EQ(std::string(buffer.data(), buffer.size()), "abc");
    }
    buffer.reset(17);
    EXPECT_EQ(buffer.capacity(), 256U);
    EXPECT_EQ(buffer.size(), 3U);
    EXPECT_EQ(buffer.data_size(), 3U);
    ASSERT_NE(buffer.buf(), nullptr);
    EXPECT_NE(reinterpret_cast<std::uintptr_t>(buffer.buf()), original_address);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffer.buf()) % 64, 0U);
    EXPECT_EQ(std::string(buffer.data(), buffer.size()), "abc");
    for (unsigned int i = buffer.size(); i < buffer.capacity(); ++i) {
        EXPECT_EQ(buffer.buf()[i], '\0');
    }
    ASSERT_TRUE(buffer.append("def", 3));
    EXPECT_EQ(std::string(buffer.data(), buffer.size()), "abcdef");
    const auto grown_address = reinterpret_cast<std::uintptr_t>(buffer.buf());
    buffer.reset(255);
    buffer.reset(256);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffer.buf()), grown_address);
    EXPECT_EQ(buffer.capacity(), 256U);
    buffer.reset(257);
    EXPECT_EQ(buffer.capacity(), 512U);
    EXPECT_EQ(buffer.size(), 6U);
    EXPECT_EQ(std::string(buffer.data(), buffer.size()), "abcdef");
    detail::buffer_t empty;
    empty.reset(0);
    EXPECT_EQ(empty.buf(), nullptr);
    empty.reset(5);
    EXPECT_EQ(empty.capacity(), 256U);
    EXPECT_EQ(empty.size(), 0U);
    EXPECT_TRUE(empty.append("abc", 3));
}
TEST(OwnedBuffer, ResetRejectsCapacityRoundingOverflowWithoutChanges) {
    detail::buffer_t buffer(8);
    ASSERT_TRUE(buffer.append("abc", 3));
    const auto original_address = reinterpret_cast<std::uintptr_t>(buffer.buf());
    EXPECT_THROW(buffer.reset(std::numeric_limits<detail::buffer_t::size_type>::max()),
                 std::length_error);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffer.buf()), original_address);
    EXPECT_EQ(buffer.capacity(), 8U);
    EXPECT_EQ(buffer.size(), 3U);
    EXPECT_EQ(std::string(buffer.data(), buffer.size()), "abc");
}
TEST(OwnedBuffer, PeakRejectsOffsetsOutsideCapacity) {
    detail::buffer_t buffer(8);
    EXPECT_EQ(buffer.peak(0), buffer.buf());
    EXPECT_EQ(buffer.peak(7), buffer.buf() + 7);
    EXPECT_EQ(buffer.peak(8), nullptr);
    EXPECT_EQ(buffer.peak(9), nullptr);
    EXPECT_EQ(buffer.peak(std::numeric_limits<detail::buffer_t::size_type>::max()), nullptr);
    ASSERT_TRUE(buffer.append("abc", 3));
    EXPECT_EQ(buffer.peak(2), buffer.buf() + 2);
    EXPECT_EQ(buffer.peak(3), buffer.buf() + 3);
    buffer.clear();
    EXPECT_EQ(buffer.peak(7), buffer.buf() + 7);
    buffer.resize(3);
    EXPECT_EQ(buffer.peak(2), buffer.buf() + 2);
    EXPECT_EQ(buffer.peak(3), nullptr);
    detail::buffer_t moved(std::move(buffer));
    EXPECT_EQ(buffer.peak(0), nullptr);
    EXPECT_EQ(moved.peak(2), moved.buf() + 2);
    moved.resize(0);
    EXPECT_EQ(moved.peak(0), nullptr);
    detail::buffer_t empty;
    EXPECT_EQ(empty.peak(0), nullptr);
    EXPECT_EQ(empty.peak(1), nullptr);
}
TEST(MutableBuffer, BoundedAppendConsumeAndOverlap) {
    char data[8]{};
    basic::mutable_buffer_t buffer(data, sizeof(data));
    EXPECT_TRUE(buffer.try_append("abc", 3));
    EXPECT_TRUE(buffer.try_append(data, 3));
    EXPECT_FALSE(buffer.try_append("xyz", 3));
    EXPECT_FALSE(buffer.consume(7));
    EXPECT_EQ(buffer.size(), 6U);
    EXPECT_TRUE(buffer.consume(2));
    EXPECT_EQ(std::string(buffer.data(), buffer.size()), "cabc");
    buffer.peek(5);
    EXPECT_TRUE(buffer.empty());
    basic::mutable_buffer_t empty(nullptr, 8);
    EXPECT_EQ(empty.capacity(), 0U);
    EXPECT_TRUE(empty.try_append(nullptr, 0));
    EXPECT_FALSE(empty.try_append("a", 1));
}
TEST(MemoryPool, AlignsExpandsReusesAndRejectsForeignPointers) {
    basic::MemoryPool<512> pool;
    void *first = pool.allocate(64);
    void *original = first;
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(first) % 64, 0U);
    void *second = pool.allocate(64);
    pool.deallocate(first);
    EXPECT_EQ(first, nullptr);
    first = pool.allocate(64);
    EXPECT_EQ(first, original);
    pool.deallocate(first);
    pool.deallocate(second);
    void *large = pool.allocate(1024, 256);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(large) % 256, 0U);
    std::memset(large, 7, 1024);
    pool.deallocate(large);
    EXPECT_EQ(pool.allocate(0), nullptr);
    EXPECT_THROW(pool.allocate(1, 3), std::invalid_argument);
    void *foreign = &pool;
    EXPECT_THROW(pool.deallocate(foreign), std::invalid_argument);
}
TEST(MemoryPool, RandomizedReusePreservesLiveAllocations) {
    basic::MemoryPool<1024, 16> pool;
    struct Block { void *ptr; std::size_t size; unsigned char value; };
    std::vector<Block> blocks;
    std::mt19937 random(20261014);
    for (int step = 0; step < 1500; ++step) {
        if (!blocks.empty() && random() % 3 == 0) {
            const auto index = random() % blocks.size();
            auto block = blocks[index];
            for (std::size_t i = 0; i < block.size; ++i)
                ASSERT_EQ(static_cast<unsigned char *>(block.ptr)[i], block.value);
            pool.deallocate(block.ptr);
            blocks.erase(blocks.begin() + index);
        } else {
            std::size_t size = 1 + random() % 1500;
            std::size_t alignment = std::size_t{1} << (4 + random() % 5);
            auto *ptr = pool.allocate(size, alignment);
            ASSERT_EQ(reinterpret_cast<std::uintptr_t>(ptr) % alignment, 0U);
            auto value = static_cast<unsigned char>(step);
            std::memset(ptr, value, size);
            blocks.push_back({ptr, size, value});
        }
    }
    for (auto block : blocks) {
        for (std::size_t i = 0; i < block.size; ++i)
            ASSERT_EQ(static_cast<unsigned char *>(block.ptr)[i], block.value);
        pool.deallocate(block.ptr);
    }
}
TEST(MemoryPool, ConcurrentAllocationAndReturn) {
    basic::MemoryPool<4096> pool;
    std::array<std::thread, 4> workers;
    for (auto &worker : workers) worker = std::thread([&] {
        for (int i = 0; i < 500; ++i) {
            void *ptr = pool.allocate(50);
            std::memset(ptr, i, 50);
            pool.deallocate(ptr);
        }
    });
    for (auto &worker : workers) worker.join();
}
TEST(FixedMemoryPool, GrowsClassesAndRecyclesAlignedBlocks) {
    detail::FixedMemoryPool<32, 256, 2> pool;
    void *a = pool.allocate(33, 128);
    void *b = pool.allocate(33, 128);
    void *c = pool.allocate(33, 128);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(a) % 128, 0U);
    void *saved = a;
    void *duplicate = a;
    EXPECT_THROW(pool.deallocate(a, 1), std::invalid_argument);
    pool.deallocate(a, 33);
    EXPECT_EQ(a, nullptr);
    EXPECT_THROW(pool.deallocate(duplicate, 33), std::invalid_argument);
    a = pool.allocate(40, 128);
    EXPECT_EQ(a, saved);
    pool.deallocate(a, 40);
    pool.deallocate(b, 33);
    pool.deallocate(c, 33);
    void *large = pool.allocate(257, 512);
    std::memset(large, 3, 257);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(large) % 512, 0U);
    pool.deallocate(large, 257);
    EXPECT_EQ(large, nullptr);
    EXPECT_THROW(pool.allocate(1, 6), std::invalid_argument);
}
namespace {
struct alignas(128) PooledObject {
    explicit PooledObject(int *live, int value = 0) : live(live), value(value) {
        if (value < 0) throw std::runtime_error("construction failed");
        ++*live;
    }
    ~PooledObject() { --*live; }
    int *live;
    int value;
};
struct DeleteInt {
    int *calls;
    void operator()(int *ptr) const { ++*calls; delete ptr; }
};
struct CountCall { int *calls; void operator()() const { ++*calls; } };
}
TEST(BasicObjectPool, ReconstructsAndDestroysObjectsAndRejectsDuplicateReturns) {
    int live = 0;
    {
        basic::ObjectPool<PooledObject> pool;
        auto *a = pool.acquire(&live, 42);
        EXPECT_EQ(live, 1);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(a) % 128, 0U);
        auto *saved = a;
        auto *duplicate = a;
        pool.release(a);
        EXPECT_EQ(a, nullptr);
        EXPECT_EQ(live, 0);
        EXPECT_THROW(pool.release(duplicate), std::invalid_argument);
        EXPECT_THROW(pool.acquire(&live, -1), std::runtime_error);
        a = pool.acquire(&live, 7);
        EXPECT_EQ(a, saved);
        EXPECT_EQ(a->value, 7);
        EXPECT_EQ(pool.in_use(), 1U);
        (void)pool.acquire(&live, 8);
        EXPECT_EQ(pool.capacity(), 2U);
    }
    EXPECT_EQ(live, 0);
}
TEST(FixedVector, KeepsAddressesAcrossGrowthAndMovesUniqueObjects) {
    basic::FixedVector<std::unique_ptr<int>, 0, 2> values;
    auto *first = &values.emplace_back(std::make_unique<int>(42));
    for (int i = 0; i < 40; ++i) values.emplace_back(std::make_unique<int>(i));
    EXPECT_EQ(first, &values[0]);
    EXPECT_EQ(**first, 42);
    auto moved = std::move(values);
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(&moved[0], first);
    EXPECT_THROW(moved.at(moved.size()), std::out_of_range);
    moved.pop_back();
    moved.clear();
    EXPECT_TRUE(moved.empty());
    EXPECT_GT(moved.capacity(), 0U);
    EXPECT_THROW(moved.pop_back(), std::out_of_range);
}
TEST(FixedVector, HandlesThrowingConstructionWithoutLosingElements) {
    int live = 0;
    {
        basic::FixedVector<PooledObject, 0, 2> values;
        values.emplace_back(&live, 1);
        EXPECT_THROW(values.emplace_back(&live, -1), std::runtime_error);
        EXPECT_EQ(values.size(), 1U);
        EXPECT_EQ(live, 1);
        values.emplace_back(&live, 2);
    }
    EXPECT_EQ(live, 0);
}
TEST(AutoPtr, MoveTransfersCleanupAndAssignmentReleasesPreviousResource) {
    int calls = 0;
    {
        detail::AutoPtr<int, DeleteInt> first(new int(1), DeleteInt{&calls});
        auto moved = std::move(first);
        detail::AutoPtr<int, DeleteInt> second(new int(2), DeleteInt{&calls});
        second = std::move(moved);
        EXPECT_EQ(calls, 1);
    }
    EXPECT_EQ(calls, 2);
}
TEST(ScopeFunc, MoveRunsEachCleanupOnce) {
    int calls = 0;
    {
        detail::ScopeFunc<CountCall> first(CountCall{&calls});
        auto moved = std::move(first);
        detail::ScopeFunc<CountCall> second(CountCall{&calls});
        second = std::move(moved);
        EXPECT_EQ(calls, 1);
    }
    EXPECT_EQ(calls, 2);
}

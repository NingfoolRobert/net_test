#include "swiss_table.h"
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <unordered_map>

TEST(SwissTable, GrowsErasesCopiesAndIterates) {
    HashMap<int, std::string> map;
    for (int i = 0; i < 600; ++i) map[i] = std::to_string(i);
    EXPECT_EQ(map.size(), 600U);
    for (int i = 0; i < 600; i += 2) map.erase(i);
    EXPECT_EQ(map.size(), 300U);
    auto copy = map;
    map[1] = "updated";
    EXPECT_EQ(copy.at(1), "1");
    const auto &constant = copy;
    std::size_t count = 0;
    for (const auto &entry : constant) {
        EXPECT_EQ(entry.first % 2, 1);
        EXPECT_EQ(entry.second, std::to_string(entry.first));
        ++count;
    }
    EXPECT_EQ(count, copy.size());
    EXPECT_THROW(constant.at(0), std::out_of_range);
    copy.clear();
    EXPECT_TRUE(copy.empty());
    copy[42] = "again";
    EXPECT_EQ(copy.at(42), "again");
}
struct CollidingHash { std::size_t operator()(int) const noexcept { return 1; } };
TEST(SwissTable, PreservesProbeChainsAfterErasingAndReinserting) {
    HashMap<int, int, CollidingHash> map;
    for (int i = 0; i < 100; ++i) map[i] = i * 2;
    for (int i = 0; i < 50; ++i) map.erase(i);
    for (int i = 50; i < 100; ++i) EXPECT_EQ(map.at(i), i * 2);
    for (int i = 100; i < 150; ++i) map[i] = i * 2;
    EXPECT_EQ(map.size(), 100U);
    for (int i = 50; i < 150; ++i) EXPECT_EQ(map.at(i), i * 2);
}
TEST(SwissTable, RandomOperationsMatchStandardMap) {
    HashMap<int, int> actual;
    std::unordered_map<int, int> expected;
    std::mt19937 random(42);
    for (int i = 0; i < 5000; ++i) {
        int key = random() % 200;
        if (random() % 2) {
            int value = random() % 1000;
            actual[key] = value;
            expected[key] = value;
        } else {
            actual.erase(key);
            expected.erase(key);
        }
        ASSERT_EQ(actual.size(), expected.size());
        for (const auto &entry : expected) ASSERT_EQ(actual.at(entry.first), entry.second);
    }
}

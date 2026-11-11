#include "../fixed_bit_set.h"

#include <gtest/gtest.h>

TEST(FixedBitSet, Basic) {
    constexpr static size_t SIZE = 100;
    detail::fixed_bit_set_t<SIZE> fbm;

    for (size_t i = 0; i < SIZE; ++i) {
        EXPECT_FALSE(fbm.test(i));
    }

    for (size_t i = 0; i < SIZE; i += 2) {
        fbm.set(i);
    }

    for (size_t i = 0; i < SIZE; ++i) {
        if (i % 2 == 0) {
            EXPECT_TRUE(fbm.test(i));
        }
        else {
            EXPECT_FALSE(fbm.test(i));
        }
    }

    for (size_t i = 0; i < SIZE; i += 3) {
        fbm.clear(i);
    }

    for (size_t i = 0; i < SIZE; ++i) {
        if (i % 2 == 0 && i % 3 != 0) {
            EXPECT_TRUE(fbm.test(i));
        }
        else {
            EXPECT_FALSE(fbm.test(i));
        }
    }
}

TEST(FixedBitMapFind, TestFind) {
    constexpr size_t SIZE = 100;
    detail::FixedBitSet<SIZE> fbm;

    EXPECT_EQ(fbm.find_first_set(), detail::FixedBitSet<SIZE>::npos);
    EXPECT_EQ(fbm.find_last_set(), detail::FixedBitSet<SIZE>::npos);

    fbm.set(98);
    EXPECT_EQ(fbm.find_first_set(), 98);
    EXPECT_EQ(fbm.find_last_set(), 98);
    EXPECT_EQ(fbm.find_first_set(98), 98);
    EXPECT_EQ(fbm.find_first_set(99), detail::FixedBitSet<SIZE>::npos);
    // EXPECT_EQ(fbm.find_last_set(98), 98);
    EXPECT_EQ(fbm.find_last_set(97), detail::FixedBitSet<SIZE>::npos);
}
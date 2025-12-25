#include "../repeat_limit.h"
#include <gtest/gtest.h>


TEST(test_repeat_limit, repeat_filter){
    basic::RepeatLimit rl(5, 10);
    
    EXPECT_EQ(rl.counter(), 0);
    EXPECT_EQ(rl.repeat(), true);  // 1
    EXPECT_EQ(rl.repeat(), true);  // 2
    EXPECT_EQ(rl.repeat(), true);  // 3
    EXPECT_EQ(rl.repeat(), true);  // 4
    EXPECT_EQ(rl.repeat(), true);  // 5
    EXPECT_EQ(rl.repeat(), false); // 6
    EXPECT_EQ(rl.repeat(), false); // 7
    EXPECT_EQ(rl.repeat(), false); // 8
    EXPECT_EQ(rl.repeat(), false); // 9
    EXPECT_EQ(rl.repeat(), false); // 10
    EXPECT_EQ(rl.repeat(), false);  // 11
    EXPECT_EQ(rl.repeat(), false);   // 12
    EXPECT_EQ(rl.repeat(), false);  // 13
    EXPECT_EQ(rl.repeat(), false);  // 14
    EXPECT_EQ(rl.repeat(), true);  // 15
}
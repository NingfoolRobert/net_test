/**
 * @brief [No code was provided in the selection to document.]
 */

#include "../raii.h"
#include <gtest/gtest.h>

TEST(RAIITest, ScopeFunc) {
    int lock = 0;
    {
        detail::RAII _([&]() { lock = 1; });
    }
    EXPECT_EQ(lock, 1);
}
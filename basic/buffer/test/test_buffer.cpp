#include "../staging_buffer.h"
#include <string.h>
#include <gtest/gtest.h>

TEST(BufferTest, BasicAllocation) {

    using namespace detail;

    StagingBuffer<128> alloc;

    auto ptr = alloc.allocate(100);
    ASSERT_NE(ptr, nullptr);
    memcpy(ptr, "Hello, World!", 14);

    alloc.deallocate(100);

    ptr = alloc.allocate(127);
    ASSERT_NE(ptr, nullptr);

    alloc.deallocate(127);

    ptr = alloc.allocate(100);
    ASSERT_NE(ptr, nullptr);

    alloc.deallocate(100);
}

// int main() {

//     using namespace detail;

//     StagingBuffer<128> alloc;

//     for (auto i = 0; i < 10; ++i) {

//         auto ptr = alloc.allocate(100);
//         memcpy(ptr, "101202011", 10);

//         alloc.deallocate(100);

//         ptr = alloc.allocate(127);

//         alloc.deallocate(127);

//         ptr = alloc.allocate(100);

//         alloc.deallocate(100);
//     }
//     return 0;
// }

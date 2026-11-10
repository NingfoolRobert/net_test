#include "../error_code.h"
#include <gtest/gtest.h>
#include <climits>
#include <atomic>
#include <thread>

TEST(IoErrorCode, RepresentsSuccessAndNativeErrors) {
    constexpr detail::io::ErrorCode success;
    constexpr detail::io::ErrorCode failure(EAGAIN);
    static_assert(success.value() == 0 && !success.has_error());
    static_assert(failure.value() == EAGAIN && failure.has_error());
    EXPECT_FALSE(static_cast<bool>(success));
    EXPECT_TRUE(static_cast<bool>(failure));
    EXPECT_EQ(failure, detail::io::ErrorCode(EAGAIN));
    EXPECT_NE(failure, detail::io::ErrorCode(EBADF));
}

TEST(IoErrorCode, DescribesKnownAndUnknownErrorsWithoutChangingErrno) {
    for (int value : {0, EBADF, EAGAIN, INT_MAX, -1}) {
        const detail::io::ErrorCode error(value);
        errno = EACCES;
        const auto message = error.message();
        EXPECT_EQ(errno, EACCES);
        EXPECT_FALSE(message.empty());
        EXPECT_EQ(error.value(), value);
        if (value == INT_MAX || value == -1)
            EXPECT_NE(message.find(std::to_string(value)), std::string::npos);
    }
}

TEST(IoErrorCode, ConcurrentDescriptionsPreserveEachThreadsErrno) {
    const detail::io::ErrorCode error(INT_MAX);
    std::atomic<int> failures{0};
    auto check = [&](int marker) {
        for (int i = 0; i < 100; ++i) {
            errno = marker;
            const auto message = error.message();
            if (errno != marker || message.find(std::to_string(INT_MAX)) == std::string::npos)
                ++failures;
        }
    };
    std::thread first(check, EACCES);
    std::thread second(check, ENOENT);
    first.join();
    second.join();
    EXPECT_EQ(failures, 0);
}

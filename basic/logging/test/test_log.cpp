#include "../logging.h"

#include <gtest/gtest.h>
#include <string>

#ifdef __linux__
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

TEST(LoggingTest, InfoIncludesCallingThreadId) {
    long tid = 0;
    testing::internal::CaptureStdout();
    std::thread worker([&tid] {
        tid = ::syscall(SYS_gettid);
        NLOG_INFO("worker %d", 42);
    });
    worker.join();
    const auto output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find(" " + std::to_string(tid) + " [INFO] worker 42\n"), std::string::npos);
}
#endif

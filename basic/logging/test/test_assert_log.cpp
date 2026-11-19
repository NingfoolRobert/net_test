#include "../../base/assert_macro.h"

#include <gtest/gtest.h>
#include <string>

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>

TEST(LoggingTest, FailedCheckLogsWarningFromAnotherTranslationUnit) {
    testing::internal::CaptureStdout();
    [] { assert_true2(false); }();
    const auto output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find(" " + std::to_string(::syscall(SYS_gettid)) + " [WARN] ["), std::string::npos);
    EXPECT_NE(output.find("] CHECK FAIL: false == false\n"), std::string::npos);
}
#endif

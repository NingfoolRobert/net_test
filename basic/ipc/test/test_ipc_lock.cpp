#include "../ipc_lock.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <future>
#include <mutex>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {

class IpcLockTest : public ::testing::Test {
protected:
    void SetUp() override {
        char name[] = "/tmp/net-test-ipc-lock-XXXXXX";
        int fd = ::mkstemp(name);
        ASSERT_NE(fd, -1);
        path = name;
        ASSERT_EQ(::close(fd), 0);
    }
    void TearDown() override {
        if (!path.empty()) {
            EXPECT_EQ(::unlink(path.c_str()), 0);
        }
    }

    std::string path;
};

TEST_F(IpcLockTest, ExcludesThreadsUsingTheSameInstance) {
    ipc_lock lock(path);
    std::unique_lock<ipc_lock> owner(lock);
    std::promise<bool> first_attempt;
    std::thread contender([&] {
        std::unique_lock<ipc_lock> attempt(lock, std::try_to_lock);
        if (attempt.owns_lock()) {
            attempt.unlock();
            first_attempt.set_value(true);
            return;
        }
        first_attempt.set_value(false);
        attempt.lock();
        EXPECT_TRUE(lock.owns_lock());
    });

    EXPECT_FALSE(first_attempt.get_future().get());
    EXPECT_TRUE(lock.owns_lock());
    owner.unlock();
    contender.join();
    EXPECT_FALSE(lock.owns_lock());
}

TEST_F(IpcLockTest, BlockingLockWaitsForTheOwner) {
    ipc_lock lock(path);
    std::unique_lock<ipc_lock> owner(lock);
    std::promise<void> started;
    auto contender = std::async(std::launch::async, [&] {
        started.set_value();
        std::lock_guard<ipc_lock> guard(lock);
    });
    started.get_future().wait();
    EXPECT_EQ(contender.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);
    owner.unlock();
    contender.get();
}

TEST_F(IpcLockTest, MovesHeldLockAndReleasesItForOtherInstances) {
    ipc_lock original(path);
    ipc_lock independent(path);
    original.lock();
    ipc_lock moved(std::move(original));
    EXPECT_FALSE(original.owns_lock());
    EXPECT_THROW(original.try_lock(), std::system_error);
    EXPECT_THROW(original.lock(), std::system_error);
    EXPECT_TRUE(moved.owns_lock());
    EXPECT_FALSE(independent.try_lock());
    moved.unlock();
    ASSERT_TRUE(independent.try_lock());
    independent.unlock();
    ASSERT_TRUE(moved.try_lock());
    moved.unlock();
}

TEST_F(IpcLockTest, IndependentChildCannotAcquireUntilParentReleases) {
    ipc_lock parent(path);
    parent.lock();
    auto child_can_lock = [&] {
        const pid_t child = ::fork();
        if (child == 0) {
            ipc_lock independent(path);
            ::_exit(independent.try_lock() ? 0 : 1);
        }
        int status = 0;
        if (child < 0 || ::waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
            return -1;
        }
        return WEXITSTATUS(status);
    };
    EXPECT_EQ(child_can_lock(), 1);
    parent.unlock();
    EXPECT_EQ(child_can_lock(), 0);
}

}  // namespace

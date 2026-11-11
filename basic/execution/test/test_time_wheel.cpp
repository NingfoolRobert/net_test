#include "../time_wheel.h"
#include <gtest/gtest.h>
#include <future>
#include <atomic>
#include <chrono>

using namespace std::chrono_literals;
TEST(TimeWheel, FiresAcrossLevelBoundariesAndBeyondWheelHorizon) {
    basic::TimeWheel wheel({4, 4}, 1);
    std::atomic<int> count{0};
    std::promise<void> completion;
    for (auto delay : {1, 4, 5, 16, 17, 33}) {
        wheel.add_timer(delay, [&] {
            if (++count == 6) completion.set_value();
        });
    }
    wheel.start();
    EXPECT_EQ(completion.get_future().wait_for(3s), std::future_status::ready);
    wheel.stop();
    EXPECT_EQ(count, 6);
}
TEST(TimeWheel, CancelsPendingAndRepeatingTimersFromCallback) {
    basic::TimeWheel wheel({4, 4}, 1);
    std::atomic<int> cancelled_calls{0}, repeat_calls{0};
    auto cancelled = wheel.add_timer(2, [&] { ++cancelled_calls; });
    wheel.cancel_timer(cancelled);
    std::promise<void> completion;
    uint64_t repeat_id = wheel.add_timer(1, [&] {
        if (++repeat_calls == 3) {
            wheel.cancel_timer(repeat_id);
            completion.set_value();
        }
    }, true, 1);
    wheel.start();
    EXPECT_EQ(completion.get_future().wait_for(3s), std::future_status::ready);
    wheel.stop();
    EXPECT_EQ(cancelled_calls, 0);
    EXPECT_EQ(repeat_calls, 3);
}
TEST(TimeWheel, HandlesSelfStopExceptionsAndRestart) {
    basic::TimeWheel wheel({4}, 1);
    wheel.add_timer(1, [] { throw std::runtime_error("callback failure"); });
    std::promise<void> stopped;
    wheel.add_timer(2, [&] { wheel.stop(); stopped.set_value(); });
    wheel.start();
    EXPECT_EQ(stopped.get_future().wait_for(3s), std::future_status::ready);
    wheel.stop();
    std::promise<void> restarted;
    wheel.add_timer(1, [&] { restarted.set_value(); });
    wheel.start();
    EXPECT_EQ(restarted.get_future().wait_for(3s), std::future_status::ready);
    wheel.stop();
}
TEST(TimeWheel, StopClearsUnstartedTimersAndInvalidArgumentsAreRejected) {
    basic::TimeWheel wheel({4}, 1);
    std::atomic<int> count{0};
    wheel.add_timer(1, [&] { ++count; });
    wheel.stop();
    std::promise<void> completion;
    wheel.add_timer(2, [&] { completion.set_value(); });
    wheel.start();
    EXPECT_EQ(completion.get_future().wait_for(3s), std::future_status::ready);
    wheel.stop();
    EXPECT_EQ(count, 0);
    EXPECT_THROW(basic::TimeWheel({}, 1), std::invalid_argument);
    EXPECT_THROW(basic::TimeWheel({0}, 1), std::invalid_argument);
    EXPECT_THROW(basic::TimeWheel({4}, 0), std::invalid_argument);
    EXPECT_THROW(wheel.add_timer(1, [] {}, true, 0), std::invalid_argument);
}

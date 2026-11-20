#include "../time_wheel.h"
#include <gtest/gtest.h>
#include <future>
#include <atomic>
#include <chrono>

using namespace std::chrono_literals;
TEST(TimeWheel, BackloggedTicksDoNotShortenNewTimerDelay) {
    using Clock = std::chrono::steady_clock;
    std::promise<void> entered, release;
    auto gate = release.get_future();
    std::promise<Clock::time_point> fired;
    auto result = fired.get_future();
    basic::TimeWheel wheel({256, 16}, 1);
    wheel.add_timer(1, [&] {
        entered.set_value();
        gate.wait_for(5s);
    });
    wheel.start();
    ASSERT_EQ(entered.get_future().wait_for(3s), std::future_status::ready);
    std::this_thread::sleep_for(250ms);
    const auto registered = Clock::now();
    wheel.add_timer(100, [&] { fired.set_value(Clock::now()); });
    release.set_value();
    ASSERT_EQ(result.wait_for(3s), std::future_status::ready);
    EXPECT_GE(std::chrono::duration_cast<std::chrono::microseconds>(result.get() - registered).count(), 100000);
}

TEST(TimeWheel, FirstExpiryRoundsTheDeadlineUpToAWheelTick) {
    using basic::time_wheel_detail::first_expiry_tick;
    EXPECT_EQ(first_expiry_tick(0, 29ms, 10, 10), 4U);
    EXPECT_EQ(first_expiry_tick(0, 20ms, 10, 10), 3U);
    EXPECT_EQ(first_expiry_tick(0, 20ms + 1us, 10, 10), 4U);
    EXPECT_EQ(first_expiry_tick(0, 19ms + 999us, 10, 10), 3U);
    EXPECT_EQ(first_expiry_tick(0, 20ms, 0, 10), 3U);
    EXPECT_EQ(first_expiry_tick(7, 29ms, 0, 10), 10U);
    const auto max = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ(first_expiry_tick(0, 0ms, max, 1), max);
    EXPECT_EQ(first_expiry_tick(0, 1ms, max, 2), max / 2 + 1);
    EXPECT_THROW(first_expiry_tick(0, 1us, max, 1), std::overflow_error);
    EXPECT_THROW(first_expiry_tick(max, 0ms, 0, 10), std::overflow_error);
}

TEST(TimeWheel, TimersAddedBeforeEachStartCountFromThatStart) {
    using Clock = std::chrono::steady_clock;
    basic::TimeWheel wheel({16}, 5);
    for (int run = 0; run < 2; ++run) {
        auto fired = std::make_shared<std::promise<Clock::time_point>>();
        auto result = fired->get_future();
        wheel.add_timer(20, [fired] { fired->set_value(Clock::now()); });
        std::this_thread::sleep_for(30ms);
        const auto started = Clock::now();
        wheel.start();
        ASSERT_EQ(result.wait_for(3s), std::future_status::ready);
        EXPECT_GE(result.get() - started, 20ms);
        wheel.stop();
    }
}

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

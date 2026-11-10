#include <gtest/gtest.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "../polling_thread_executor.h"

namespace {
std::atomic<int> affinity_failures{0};
thread_local int allocation_countdown = -1;
std::atomic<bool> watch_sleep{false};
std::atomic<bool> entered_sleep{false};
std::atomic<bool> release_sleep{false};
}  // namespace

// GNU ld --wrap requires these exact reserved symbol names.
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" void* __real__Znwm(size_t size);
extern "C" void* __wrap__Znwm(size_t size) {
  if (allocation_countdown == 0) {
    allocation_countdown = -1;
    throw std::bad_alloc();
  }
  if (allocation_countdown > 0) {
    --allocation_countdown;
  }
  return __real__Znwm(size);
}

extern "C" int __real_nanosleep(const timespec*, timespec*);
extern "C" int __wrap_nanosleep(const timespec* requested,
                                timespec* remaining) {
  if (requested->tv_sec == 0 && requested->tv_nsec == 100000000 &&
      watch_sleep.exchange(false)) {
    entered_sleep.store(true);
    while (!release_sleep.load()) {
      std::this_thread::yield();
    }
  }
  return __real_nanosleep(requested, remaining);
}

extern "C" int __real_pthread_setaffinity_np(pthread_t, size_t,
                                             const cpu_set_t*);
extern "C" int __wrap_pthread_setaffinity_np(pthread_t thread, size_t size,
                                             const cpu_set_t* mask) {
  int remaining = affinity_failures.load();
  while (remaining > 0) {
    if (affinity_failures.compare_exchange_weak(remaining, remaining - 1)) {
      return EINVAL;
    }
  }
  return __real_pthread_setaffinity_np(thread, size, mask);
}

// NOLINTEND(bugprone-reserved-identifier)

namespace basic {
namespace {
using namespace std::chrono_literals;

// Release and join before destroying the gate: the callback captures this gate.
// This also keeps assertion failures from leaving the executor blocked.
class CallbackGate {
 public:
  explicit CallbackGate(PollingThreadExecutor& executor)
      : executor_(executor) {}
  ~CallbackGate() {
    Release();
    executor_.Stop();
  }
  void Block() {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    changed_.notify_all();
    changed_.wait(lock, [this] { return released_; });
  }
  bool WaitEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 3s, [this] { return entered_; });
  }
  void Release() {
    const std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    changed_.notify_all();
  }

 private:
  PollingThreadExecutor& executor_;
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool released_ = false;
};

template <typename Predicate>
bool Eventually(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

int FirstAllowedCpu() {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(mask), &mask) != 0) {
    throw std::system_error(errno, std::generic_category());
  }
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &mask)) {
      return cpu;
    }
  }
  throw std::runtime_error("no allowed CPU");
}

struct DestructionProbe {
  explicit DestructionProbe(std::atomic<bool>& flag) : destroyed(flag) {}
  ~DestructionProbe() { destroyed.store(true); }
  std::atomic<bool>& destroyed;
};

// Inspect native settings in a callback without exposing a query API.
void CheckOnWorker(PollingThreadExecutor& executor, Handler check) {
  std::promise<void> done;
  auto finished = done.get_future();
  bool called = false;
  HandlerCookie* cookie = nullptr;
  ASSERT_TRUE(executor.AddHandler(cookie, [&] {
    if (!called) {
      called = true;
      check();
      done.set_value();
    }
  }));
  EXPECT_EQ(finished.wait_for(3s), std::future_status::ready);
  EXPECT_TRUE(executor.RemoveHandler(cookie));
}

TEST(PollingThreadExecutorTest, StartupStopAndDiagnostics) {
  PollingThreadExecutor executor;
  EXPECT_TRUE(executor.GetError().empty());
  ASSERT_TRUE(executor.Start());
  EXPECT_TRUE(executor.Start());
  CheckOnWorker(executor, [] {
    int policy = 0;
    sched_param param{};
    ASSERT_EQ(pthread_getschedparam(pthread_self(), &policy, &param), 0);
    EXPECT_EQ(policy, SCHED_OTHER);
    EXPECT_EQ(param.sched_priority, 0);
  });
  executor.Stop();
  executor.Stop();
  EXPECT_FALSE(executor.Start());
  ASSERT_FALSE(executor.GetError().empty());
  EXPECT_EQ(executor.GetError(), "executor has been requested to stop");
}

TEST(PollingThreadExecutorTest, ConfigurationValidationAndIgnoredFields) {
  ThreadExecutorConfig config;
  config.cpu = -2;
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.cpu = -1;
  config.name = "more-than-fifteen-bytes";
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.name = std::string("a\0b", 3);
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.name.clear();
  // Intentionally invalid input exercises constructor validation.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  config.scheduling_policy = static_cast<SchedulingPolicy>(99);
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.scheduling_policy = SchedulingPolicy::kOther;
  // Intentionally invalid input exercises constructor validation.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  config.nice_value = static_cast<NicePriority>(99);
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.nice_value = NicePriority::kMedium;
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  config.priority = static_cast<RealtimePriority>(100);
  config.sleep_us = -1us;
  {
    PollingThreadExecutor executor(config);
    ASSERT_TRUE(executor.Start());
  }
  config.scheduling_policy = SchedulingPolicy::kFifo;
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.priority = RealtimePriority::kMedium;
  // Intentionally invalid input exercises constructor validation.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  config.nice_value = static_cast<NicePriority>(99);
  EXPECT_NO_THROW(PollingThreadExecutor{config});
  config.scheduling_policy = SchedulingPolicy::kOther;
  config.nice_value = NicePriority::kMedium;
  config.idle_strategy = IdleStrategy::kIdle;
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.sleep_us = std::chrono::hours(25);
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
  config.sleep_us = 1us;
  // Intentionally invalid input exercises constructor validation.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  config.idle_strategy = static_cast<IdleStrategy>(99);
  EXPECT_THROW(PollingThreadExecutor{config}, std::invalid_argument);
}

TEST(PollingThreadExecutorTest, FailedAffinityStartupCanRetrySuccessfully) {
  ThreadExecutorConfig config;
  config.cpu = FirstAllowedCpu();
  config.name = "polling-v1";
  PollingThreadExecutor executor(config);
  config.name = "changed";
  config.cpu = CPU_SETSIZE;
  affinity_failures.store(1);
  EXPECT_FALSE(executor.Start());
  const auto error = executor.GetError();
  EXPECT_NE(error.find("pthread_setaffinity_np"), std::string::npos);
  EXPECT_NE(error.find(std::generic_category().message(EINVAL)),
            std::string::npos);
  ASSERT_TRUE(executor.Start());
  EXPECT_TRUE(executor.GetError().empty());
  const int expected_cpu = FirstAllowedCpu();
  CheckOnWorker(executor, [expected_cpu] {
    std::array<char, 16> name{};
    ASSERT_EQ(pthread_getname_np(pthread_self(), name.data(), name.size()), 0);
    EXPECT_STREQ(name.data(), "polling-v1");
    cpu_set_t mask;
    CPU_ZERO(&mask);
    ASSERT_EQ(pthread_getaffinity_np(pthread_self(), sizeof(mask), &mask), 0);
    EXPECT_EQ(CPU_COUNT(&mask), 1);
    EXPECT_TRUE(CPU_ISSET(expected_cpu, &mask));
  });
}

TEST(PollingThreadExecutorTest, UnsupportedCpuFailsWithoutOpeningAdmission) {
  ThreadExecutorConfig config;
  config.cpu = CPU_SETSIZE;
  PollingThreadExecutor executor(config);
  EXPECT_FALSE(executor.Start());
  HandlerCookie* cookie = nullptr;
  EXPECT_FALSE(executor.AddHandler(cookie, [] {}));
  EXPECT_EQ(cookie, nullptr);
  EXPECT_FALSE(executor.Start());
  ASSERT_FALSE(executor.GetError().empty());
  EXPECT_NE(executor.GetError().find("CPU is not supported"),
            std::string::npos);
}

TEST(PollingThreadExecutorTest, StopBeforeStartIsTerminal) {
  PollingThreadExecutor executor;
  executor.Stop();
  executor.Stop();
  EXPECT_FALSE(executor.Start());
}

TEST(PollingThreadExecutorTest, DefaultNicePreservesInheritedNonzeroValue) {
  std::thread parent([] {
    ASSERT_EQ(setpriority(PRIO_PROCESS, 0, 10), 0);
    PollingThreadExecutor executor;
    ASSERT_TRUE(executor.Start());
    CheckOnWorker(executor, [] {
      errno = 0;
      EXPECT_EQ(getpriority(PRIO_PROCESS, 0), 10);
      EXPECT_EQ(errno, 0);
    });
  });
  parent.join();
}

TEST(PollingThreadExecutorTest, ExplicitNiceAndRealtimeConfiguration) {
  ThreadExecutorConfig config;
  config.nice_value = NicePriority::kLower;
  {
    PollingThreadExecutor executor(config);
    ASSERT_TRUE(executor.Start());
    CheckOnWorker(executor, [] {
      errno = 0;
      EXPECT_EQ(getpriority(PRIO_PROCESS, 0), 10);
      EXPECT_EQ(errno, 0);
    });
  }
  config.scheduling_policy = SchedulingPolicy::kFifo;
  config.cpu = FirstAllowedCpu();
  // Intentionally invalid input exercises constructor validation.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  config.nice_value = static_cast<NicePriority>(999);
  PollingThreadExecutor executor(config);
  if (executor.Start()) {
    CheckOnWorker(executor, [] {
      int policy = 0;
      sched_param param{};
      ASSERT_EQ(pthread_getschedparam(pthread_self(), &policy, &param), 0);
      EXPECT_EQ(param.sched_priority, 50);
      EXPECT_EQ(policy, SCHED_FIFO);
    });
  } else {
    ASSERT_FALSE(executor.GetError().empty());
    const auto error = executor.GetError();
    EXPECT_NE(error.find("pthread_setschedparam"), std::string::npos);
    EXPECT_TRUE(error.find(std::generic_category().message(EPERM)) !=
                    std::string::npos ||
                error.find(std::generic_category().message(EACCES)) !=
                    std::string::npos);
  }
}

TEST(PollingThreadExecutorTest,
     CookieOverwriteFailurePreservationAndCompaction) {
  PollingThreadExecutor executor;
  ASSERT_TRUE(executor.Start());
  HandlerCookie* first = nullptr;
  ASSERT_TRUE(executor.AddHandler(first, [] {}, "first"));
  HandlerCookie* saved_first = first;
  ASSERT_TRUE(executor.AddHandler(first, [] {}, "middle"));
  HandlerCookie* middle = first;
  EXPECT_FALSE(executor.AddHandler(first, Handler{}));
  EXPECT_EQ(first, middle);
  HandlerCookie* tail = nullptr;
  ASSERT_TRUE(executor.AddHandler(tail, [] {}, "tail"));
  auto* tail_address = tail;
  ASSERT_TRUE(executor.RemoveHandler(middle));
  EXPECT_EQ(middle, nullptr);
  EXPECT_EQ(tail, tail_address);
  ASSERT_TRUE(executor.RemoveHandler(tail));
  ASSERT_TRUE(executor.RemoveHandler(saved_first));
  EXPECT_FALSE(executor.RemoveHandler(saved_first));
}

TEST(PollingThreadExecutorTest, ExternalSynchronousRemovalDestroysCaptures) {
  PollingThreadExecutor executor;
  ASSERT_TRUE(executor.Start());
  std::atomic<bool> destroyed{false};
  auto resource = std::make_shared<DestructionProbe>(destroyed);
  HandlerCookie* cookie = nullptr;
  ASSERT_TRUE(executor.AddHandler(cookie, [resource] {}));
  resource.reset();
  ASSERT_TRUE(executor.RemoveHandler(cookie));
  EXPECT_TRUE(destroyed.load());
  EXPECT_EQ(cookie, nullptr);
}

TEST(PollingThreadExecutorTest, PendingAddsReserveAll512Slots) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  std::vector<HandlerCookie*> cookies(PollingThreadExecutor::kMaxHandlers - 1);
  for (auto& cookie : cookies) {
    ASSERT_TRUE(executor.AddHandler(cookie, [] {}, {}, true));
  }
  HandlerCookie* output = blocker;
  EXPECT_FALSE(executor.AddHandler(output, [] {}, {}, true));
  EXPECT_EQ(output, blocker);
  gate.Release();
  ASSERT_TRUE(executor.RemoveHandler(cookies[5]));
  ASSERT_TRUE(executor.AddHandler(output, [] {}, "replacement"));
  EXPECT_NE(output, nullptr);
}

TEST(PollingThreadExecutorTest,
     WorkerSynchronouslyRemovesItselfAndOtherHandler) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  std::atomic<bool> destroyed{false};
  auto resource = std::make_shared<DestructionProbe>(destroyed);
  HandlerCookie* self = nullptr;
  HandlerCookie* other = nullptr;
  std::atomic<int> other_calls{0};
  std::promise<void> done;
  auto finished = done.get_future();
  ASSERT_TRUE(executor.AddHandler(
      self,
      [&, resource] {
        EXPECT_TRUE(executor.RemoveHandler(other));
        EXPECT_EQ(other, nullptr);
        EXPECT_TRUE(executor.RemoveHandler(self));
        EXPECT_EQ(self, nullptr);
        EXPECT_FALSE(destroyed.load());
        EXPECT_NE(resource.get(), nullptr);
        done.set_value();
      },
      "self", true));
  ASSERT_TRUE(
      executor.AddHandler(other, [&] { ++other_calls; }, "other", true));
  resource.reset();
  gate.Release();
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
  executor.Stop();
  EXPECT_EQ(other_calls.load(), 0);
  EXPECT_TRUE(destroyed.load());
}

TEST(PollingThreadExecutorTest, WorkerDirectAddStartsNextRound) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  HandlerCookie* parent = nullptr;
  HandlerCookie* child = nullptr;
  std::atomic<int> stage{0};
  std::promise<void> done;
  auto finished = done.get_future();
  ASSERT_TRUE(executor.AddHandler(
      parent,
      [&] {
        EXPECT_EQ(stage.exchange(1), 0);
        EXPECT_TRUE(executor.AddHandler(
            child,
            [&] {
              EXPECT_EQ(stage.exchange(2), 1);
              EXPECT_TRUE(executor.RemoveHandler(child));
              done.set_value();
            },
            "child"));
        EXPECT_EQ(stage.load(), 1);
        EXPECT_TRUE(executor.RemoveHandler(parent));
      },
      "parent", true));
  gate.Release();
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
  executor.Stop();
  EXPECT_EQ(stage.load(), 2);
}

TEST(PollingThreadExecutorTest,
     WorkerCancelsUnregisteredAsyncAddAndExceptionReclaimsIt) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  std::atomic<bool> destroyed{false};
  std::atomic<int> canceled_calls{0};
  HandlerCookie* self = nullptr;
  std::promise<void> done;
  auto finished = done.get_future();
  ASSERT_TRUE(executor.AddHandler(
      self,
      [&] {
        auto resource = std::make_shared<DestructionProbe>(destroyed);
        HandlerCookie* pending = nullptr;
        EXPECT_TRUE(executor.AddHandler(
            pending, [&, resource] { ++canceled_calls; }, "canceled", true));
        resource.reset();
        EXPECT_EQ(canceled_calls.load(), 0);
        EXPECT_TRUE(executor.RemoveHandler(pending));
        EXPECT_EQ(pending, nullptr);
        EXPECT_FALSE(destroyed.load());
        EXPECT_TRUE(executor.RemoveHandler(self));
        done.set_value();
        throw std::runtime_error("stop after canceling pending add");
      },
      "self", true));
  gate.Release();
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
  executor.Stop();
  EXPECT_EQ(canceled_calls.load(), 0);
  EXPECT_TRUE(destroyed.load());
  EXPECT_EQ(executor.GetError(), "stop after canceling pending add");
}

TEST(PollingThreadExecutorTest, WorkerAsyncRemovalRemainsQueued) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  HandlerCookie* first = nullptr;
  HandlerCookie* second = nullptr;
  std::promise<void> called;
  auto finished = called.get_future();
  std::atomic<int> count{0};
  ASSERT_TRUE(executor.AddHandler(
      first,
      [&] {
        EXPECT_TRUE(executor.RemoveHandler(second, true));
        EXPECT_TRUE(executor.RemoveHandler(first));
      },
      "first", true));
  ASSERT_TRUE(executor.AddHandler(
      second,
      [&] {
        if (++count == 1) {
          called.set_value();
        }
      },
      "second", true));
  gate.Release();
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
  // A synchronous deletion follows the queued deletion of second.
  ASSERT_TRUE(executor.RemoveHandler(blocker));
  EXPECT_EQ(count.load(), 1);
}

TEST(PollingThreadExecutorTest,
     FullCapacityWorkerDeleteDoesNotReleaseSlotEarly) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  HandlerCookie* self = nullptr;
  std::promise<void> done;
  auto finished = done.get_future();
  ASSERT_TRUE(executor.AddHandler(
      self,
      [&] {
        EXPECT_TRUE(executor.RemoveHandler(self));
        HandlerCookie* output = nullptr;
        EXPECT_FALSE(executor.AddHandler(output, [] {}));
        EXPECT_EQ(output, nullptr);
        done.set_value();
      },
      "self", true));
  for (std::size_t i = 2; i < PollingThreadExecutor::kMaxHandlers; ++i) {
    HandlerCookie* output = nullptr;
    ASSERT_TRUE(executor.AddHandler(output, [] {}, {}, true));
  }
  gate.Release();
  // The self callback can run before all pending adds are registered, but their
  // reserved slots must still prevent a new direct addition.
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
  executor.Stop();
}

TEST(PollingThreadExecutorTest,
     BudgetPreservesBusinessPollingAndOldBatchOrder) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  HandlerCookie* late = nullptr;
  int rounds = 0;
  std::size_t old_calls = 0;
  std::size_t first_calls = 0;
  std::promise<void> done;
  auto finished = done.get_future();
  ASSERT_TRUE(executor.AddHandler(blocker, [&] {
    ++rounds;
    if (rounds == 1) {
      gate.Block();
    } else if (rounds == 2) {
      EXPECT_EQ(old_calls, 0U);
      EXPECT_TRUE(executor.AddHandler(
          late,
          [&] {
            EXPECT_EQ(rounds, 4);
            EXPECT_EQ(first_calls,
                      2 * PollingThreadExecutor::kCommandsPerRound);
            EXPECT_TRUE(executor.RemoveHandler(late));
            done.set_value();
          },
          "late", true));
    } else if (rounds == 3) {
      EXPECT_EQ(old_calls, PollingThreadExecutor::kCommandsPerRound);
    } else if (rounds == 4) {
      EXPECT_EQ(old_calls, 3 * PollingThreadExecutor::kCommandsPerRound);
    }
  }));
  ASSERT_TRUE(gate.WaitEntered());
  for (std::size_t i = 0; i < 2 * PollingThreadExecutor::kCommandsPerRound;
       ++i) {
    HandlerCookie* cookie = nullptr;
    ASSERT_TRUE(executor.AddHandler(
        cookie,
        [&, i, first = true]() mutable {
          ++old_calls;
          if (first) {
            EXPECT_EQ(first_calls++, i);
            first = false;
          }
        },
        "old", true));
  }
  gate.Release();
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
  executor.Stop();
}

TEST(PollingThreadExecutorTest,
     BoundedQueueRejectsExcessRemovalsAndReclaimsAllCommands) {
  static_assert(PollingThreadExecutor::kMaxPendingCommands ==
                2 * PollingThreadExecutor::kMaxHandlers);
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  for (std::size_t i = 1; i < PollingThreadExecutor::kMaxHandlers; ++i) {
    HandlerCookie* cookie = nullptr;
    ASSERT_TRUE(executor.AddHandler(cookie, [] {}, {}, true));
    ASSERT_TRUE(executor.RemoveHandler(cookie, true));
  }
  ASSERT_TRUE(executor.RemoveHandler(blocker, true));
  // Foreign handles remain alive in their owner. One removal can enter the
  // last queue slot; the other must be rejected while the worker is blocked.
  PollingThreadExecutor owner;
  ASSERT_TRUE(owner.Start());
  HandlerCookie* foreign1 = nullptr;
  HandlerCookie* foreign2 = nullptr;
  ASSERT_TRUE(owner.AddHandler(foreign1, [] {}));
  ASSERT_TRUE(owner.AddHandler(foreign2, [] {}));
  auto remove = [&](HandlerCookie* foreign) {
    auto* output = foreign;
    const bool ok = executor.RemoveHandler(output);
    EXPECT_EQ(output, foreign);
    return ok;
  };
  auto first = std::async(std::launch::async, remove, foreign1);
  auto second = std::async(std::launch::async, remove, foreign2);
  const bool rejected_before_release = Eventually([&] {
    return first.wait_for(0ms) == std::future_status::ready ||
           second.wait_for(0ms) == std::future_status::ready;
  });
  const auto first_status = first.wait_for(0ms);
  const auto second_status = second.wait_for(0ms);
  gate.Release();
  EXPECT_TRUE(rejected_before_release);
  EXPECT_NE(first_status, second_status);
  EXPECT_FALSE(first.get());
  EXPECT_FALSE(second.get());
  // A synchronous command drains earlier work; all handler slots can be used.
  HandlerCookie* replacement = nullptr;
  ASSERT_TRUE(executor.AddHandler(replacement, [] {}));
  for (std::size_t i = 1; i < PollingThreadExecutor::kMaxHandlers; ++i) {
    HandlerCookie* cookie = nullptr;
    ASSERT_TRUE(executor.AddHandler(cookie, [] {}, {}, true));
  }
  EXPECT_TRUE(owner.RemoveHandler(foreign1));
  EXPECT_TRUE(owner.RemoveHandler(foreign2));
}

TEST(PollingThreadExecutorTest,
     StopClosesAdmissionAndDrainsSynchronousWaiters) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  std::atomic<bool> destroyed{false};
  auto resource = std::make_shared<DestructionProbe>(destroyed);
  HandlerCookie* queued = nullptr;
  ASSERT_TRUE(executor.AddHandler(queued, [resource] {}, "queued", true));
  resource.reset();
  auto deletion = std::async(std::launch::async,
                             [&] { return executor.RemoveHandler(queued); });
  EXPECT_EQ(deletion.wait_for(10ms), std::future_status::timeout);
  auto stop = std::async(std::launch::async, [&] { executor.Stop(); });
  // Observe admission closing while the worker is still blocked. Capacity
  // must remain available, so rejection can only be due to stopping.
  std::size_t accepted = 0;
  EXPECT_TRUE(Eventually([&] {
    HandlerCookie* output = nullptr;
    if (!executor.AddHandler(output, [] {}, {}, true)) {
      return true;
    }
    ++accepted;
    return false;
  }));
  EXPECT_LT(accepted, PollingThreadExecutor::kMaxHandlers - 2);
  HandlerCookie* rejected = blocker;
  EXPECT_FALSE(executor.AddHandler(rejected, [] {}, {}, true));
  EXPECT_EQ(rejected, blocker);
  EXPECT_EQ(stop.wait_for(10ms), std::future_status::timeout);
  gate.Release();
  // The racing delete either entered before closure or was rejected. Both must
  // finish, and shutdown must reclaim the already accepted addition regardless.
  deletion.get();
  stop.get();
  EXPECT_TRUE(destroyed.load());
}

TEST(PollingThreadExecutorTest, WorkerStopRejectsSelfWait) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  HandlerCookie* self = nullptr;
  std::promise<void> done;
  auto finished = done.get_future();
  ASSERT_TRUE(executor.AddHandler(
      self,
      [&] {
        EXPECT_TRUE(executor.Start());
        EXPECT_THROW(executor.Stop(), std::logic_error);
        EXPECT_TRUE(executor.RemoveHandler(self));
        done.set_value();
      },
      {}, true));
  gate.Release();
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
  executor.Stop();
}

TEST(PollingThreadExecutorTest,
     ForeignCookieIsRejectedAndCrossExecutorCallsAreSynchronous) {
  PollingThreadExecutor first;
  PollingThreadExecutor second;
  CallbackGate gate(first);
  ASSERT_TRUE(first.Start());
  ASSERT_TRUE(second.Start());
  HandlerCookie* foreign = nullptr;
  ASSERT_TRUE(second.AddHandler(foreign, [] {}, "foreign"));
  auto* original = foreign;
  EXPECT_FALSE(first.RemoveHandler(foreign));
  EXPECT_EQ(foreign, original);
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(first.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  HandlerCookie* local = nullptr;
  std::promise<void> done;
  auto finished = done.get_future();
  ASSERT_TRUE(first.AddHandler(
      local,
      [&] {
        EXPECT_TRUE(second.RemoveHandler(foreign));
        EXPECT_EQ(foreign, nullptr);
        EXPECT_TRUE(first.RemoveHandler(local));
        done.set_value();
      },
      {}, true));
  gate.Release();
  ASSERT_EQ(finished.wait_for(3s), std::future_status::ready);
}

TEST(PollingThreadExecutorTest,
     StandardExceptionStopsAndPreservesOriginalCause) {
  std::promise<void> entered;
  auto ready = entered.get_future();
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  HandlerCookie* throwing = nullptr;
  HandlerCookie* skipped = nullptr;
  std::atomic<int> skipped_calls{0};
  ASSERT_TRUE(executor.AddHandler(
      throwing,
      [&] {
        entered.set_value();
        throw std::runtime_error("original handler failure");
      },
      "throwing", true));
  ASSERT_TRUE(
      executor.AddHandler(skipped, [&] { ++skipped_calls; }, "skipped", true));
  gate.Release();
  ASSERT_EQ(ready.wait_for(3s), std::future_status::ready);
  executor.Stop();
  const auto error = executor.GetError();
  EXPECT_EQ(error, "original handler failure");
  EXPECT_EQ(skipped_calls.load(), 0);
  EXPECT_FALSE(executor.Start());
  auto copy = executor.GetError();
  EXPECT_EQ(copy, error);
  copy.clear();
  EXPECT_EQ(executor.GetError(), error);
}

TEST(PollingThreadExecutorTest, StopRejectionDoesNotMaskRuntimeException) {
  std::promise<void> entered;
  auto ready = entered.get_future();
  PollingThreadExecutor executor;
  ASSERT_TRUE(executor.Start());
  HandlerCookie* cookie = nullptr;
  ASSERT_TRUE(executor.AddHandler(cookie, [&] {
    entered.set_value();
    // Only the worker accesses diagnostics until Stop() has joined it.
    EXPECT_TRUE(Eventually([&] { return !executor.Start(); }));
    throw std::runtime_error("runtime failure during stop");
  }));
  ASSERT_EQ(ready.wait_for(3s), std::future_status::ready);
  executor.Stop();
  EXPECT_EQ(executor.GetError(), "runtime failure during stop");
  EXPECT_FALSE(executor.Start());
  EXPECT_EQ(executor.GetError(), "runtime failure during stop");
}

TEST(PollingThreadExecutorTest,
     SelfDeletionThenNonstandardExceptionStillReclaimsSafely) {
  std::promise<void> entered;
  auto ready = entered.get_future();
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* blocker = nullptr;
  ASSERT_TRUE(executor.AddHandler(blocker, [&] { gate.Block(); }));
  ASSERT_TRUE(gate.WaitEntered());
  std::atomic<bool> destroyed{false};
  auto resource = std::make_shared<DestructionProbe>(destroyed);
  HandlerCookie* self = nullptr;
  ASSERT_TRUE(executor.AddHandler(
      self,
      [&, resource] {
        EXPECT_TRUE(executor.RemoveHandler(self));
        EXPECT_FALSE(destroyed.load());
        entered.set_value();
        throw 42;
      },
      "self", true));
  resource.reset();
  gate.Release();
  ASSERT_EQ(ready.wait_for(3s), std::future_status::ready);
  executor.Stop();
  EXPECT_TRUE(destroyed.load());
  EXPECT_EQ(self, nullptr);
  EXPECT_EQ(executor.GetError(), "non-standard handler exception");
}

TEST(PollingThreadExecutorTest, EmptyRuntimeExceptionStillReportsError) {
  std::promise<void> entered;
  auto ready = entered.get_future();
  PollingThreadExecutor executor;
  ASSERT_TRUE(executor.Start());
  HandlerCookie* cookie = nullptr;
  ASSERT_TRUE(executor.AddHandler(cookie, [&] {
    entered.set_value();
    throw std::runtime_error("");
  }));
  ASSERT_EQ(ready.wait_for(3s), std::future_status::ready);
  executor.Stop();
  EXPECT_EQ(executor.GetError(), "handler exception");
}

TEST(PollingThreadExecutorTest,
     LongRuntimeErrorSurvivesDiagnosticAllocationFailure) {
  const std::string message(1024, 'x');
  const std::runtime_error failure(message);
  for (bool fail_allocation : {false, true}) {
    std::promise<void> entered;
    auto ready = entered.get_future();
    PollingThreadExecutor executor;
    ASSERT_TRUE(executor.Start());
    HandlerCookie* cookie = nullptr;
    ASSERT_TRUE(executor.AddHandler(cookie, [&] {
      entered.set_value();
      if (fail_allocation) {
        allocation_countdown = 0;
      }
      throw failure;
    }));
    ASSERT_EQ(ready.wait_for(3s), std::future_status::ready);
    executor.Stop();
    EXPECT_EQ(executor.GetError(),
              fail_allocation ? "handler exception" : message);
    EXPECT_FALSE(executor.Start());
    EXPECT_EQ(executor.GetError(),
              fail_allocation ? "handler exception" : message);
  }
}

TEST(PollingThreadExecutorTest,
     MultiProducerAddRemoveCompletesAcrossInstances) {
  constexpr int kInstances = 3;
  constexpr std::size_t kProducers = 4;
  constexpr std::size_t kEach = 80;
  for (int instance = 0; instance < kInstances; ++instance) {
    std::array<std::atomic<int>, kProducers * kEach> calls{};
    PollingThreadExecutor executor;
    ASSERT_TRUE(executor.Start());
    std::vector<std::vector<HandlerCookie*>> cookies(kProducers);
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (std::size_t p = 0; p < kProducers; ++p) {
      producers.emplace_back([&, p] {
        for (std::size_t i = 0; i < kEach; ++i) {
          HandlerCookie* cookie = nullptr;
          EXPECT_TRUE(executor.AddHandler(
              cookie, [&, p, i] { ++calls[p * kEach + i]; }, {}, (i % 2) != 0));
          cookies[p].push_back(cookie);
        }
      });
    }
    for (auto& producer : producers) {
      producer.join();
    }
    ASSERT_TRUE(Eventually([&] {
      return std::all_of(calls.begin(), calls.end(),
                         [](const auto& count) { return count.load() > 0; });
    }));
    producers.clear();
    for (std::size_t p = 0; p < kProducers; ++p) {
      producers.emplace_back([&, p] {
        for (auto& cookie : cookies[p]) {
          EXPECT_TRUE(executor.RemoveHandler(cookie));
          EXPECT_EQ(cookie, nullptr);
        }
      });
    }
    for (auto& producer : producers) {
      producer.join();
    }
    std::array<int, kProducers * kEach> final_counts{};
    for (std::size_t i = 0; i < calls.size(); ++i) {
      final_counts[i] = calls[i].load();
    }
    executor.Stop();
    for (std::size_t i = 0; i < calls.size(); ++i) {
      EXPECT_EQ(calls[i].load(), final_counts[i]);
    }
  }
}

TEST(PollingThreadExecutorTest, RuntimeErrorsAreIndependentAcrossInstances) {
  std::array<std::promise<void>, 3> entered;
  std::array<PollingThreadExecutor, 3> executors;
  for (std::size_t i = 0; i < executors.size(); ++i) {
    auto ready = entered[i].get_future();
    auto& executor = executors[i];
    ASSERT_TRUE(executor.Start());
    HandlerCookie* cookie = nullptr;
    ASSERT_TRUE(executor.AddHandler(cookie, [&, i] {
      entered[i].set_value();
      throw std::runtime_error("handler " + std::to_string(i));
    }));
    ASSERT_EQ(ready.wait_for(3s), std::future_status::ready);
    executor.Stop();
  }
  for (std::size_t i = 0; i < executors.size(); ++i) {
    EXPECT_EQ(executors[i].GetError(), "handler " + std::to_string(i));
  }
}

TEST(PollingThreadExecutorTest, ConcurrentStartAndStopDoNotReopenAdmission) {
  for (int i = 0; i < 50; ++i) {
    PollingThreadExecutor executor;
    std::thread startup([&] { executor.Start(); });
    std::thread stop1([&] { executor.Stop(); });
    std::thread stop2([&] { executor.Stop(); });
    startup.join();
    stop1.join();
    stop2.join();
    EXPECT_FALSE(executor.Start());
    HandlerCookie* cookie = nullptr;
    EXPECT_FALSE(executor.AddHandler(cookie, [] {}, {}, true));
  }
}

TEST(PollingThreadExecutorTest,
     AllocationFailuresPreserveOutputAndDoNotLeakSlots) {
  PollingThreadExecutor executor;
  ASSERT_TRUE(executor.Start());
  HandlerCookie* existing = nullptr;
  ASSERT_TRUE(executor.AddHandler(existing, [] {}, "existing"));
  int rejected = 0;
  for (int attempt = 0; attempt < 50; ++attempt) {
    auto* output = existing;
    allocation_countdown = attempt % 5;
    const bool ok = executor.AddHandler(output, [] {});
    allocation_countdown = -1;
    if (ok) {
      EXPECT_TRUE(executor.RemoveHandler(output));
    } else {
      ++rejected;
      EXPECT_EQ(output, existing);
    }
  }
  EXPECT_GT(rejected, 0);
  for (std::size_t i = 1; i < PollingThreadExecutor::kMaxHandlers; ++i) {
    HandlerCookie* cookie = nullptr;
    ASSERT_TRUE(executor.AddHandler(cookie, [] {}, {}, true));
  }
  executor.Stop();
}

TEST(PollingThreadExecutorTest, StartupAllocationFailureCanRetry) {
  for (int point = 0; point < 3; ++point) {
    PollingThreadExecutor executor;
    allocation_countdown = point;
    const bool ok = executor.Start();
    allocation_countdown = -1;
    EXPECT_FALSE(ok);
    ASSERT_FALSE(executor.GetError().empty());
    EXPECT_TRUE(executor.Start());
    EXPECT_TRUE(executor.GetError().empty());
  }
}

TEST(PollingThreadExecutorTest,
     RuntimeExceptionDrainsAcceptedAddsWithoutCallingThem) {
  PollingThreadExecutor executor;
  CallbackGate gate(executor);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* throwing = nullptr;
  ASSERT_TRUE(executor.AddHandler(throwing, [&] {
    gate.Block();
    throw std::runtime_error("stop with pending operations");
  }));
  ASSERT_TRUE(gate.WaitEntered());
  std::atomic<int> calls{0};
  std::atomic<bool> destroyed{false};
  auto resource = std::make_shared<DestructionProbe>(destroyed);
  HandlerCookie* pending = nullptr;
  ASSERT_TRUE(
      executor.AddHandler(pending, [&, resource] { ++calls; }, {}, true));
  resource.reset();
  auto removal = std::async(std::launch::async,
                            [&] { return executor.RemoveHandler(pending); });
  EXPECT_EQ(removal.wait_for(10ms), std::future_status::timeout);
  gate.Release();
  removal.get();
  executor.Stop();
  EXPECT_EQ(calls.load(), 0);
  EXPECT_TRUE(destroyed.load());
  EXPECT_EQ(executor.GetError(), "stop with pending operations");
}

TEST(PollingThreadExecutorTest, IdleModeProcessesCommandsWithoutEarlyWakeup) {
  ThreadExecutorConfig config;
  config.idle_strategy = IdleStrategy::kIdle;
  config.sleep_us = 10ms;
  PollingThreadExecutor executor(config);
  ASSERT_TRUE(executor.Start());
  HandlerCookie* cookie = nullptr;
  ASSERT_TRUE(executor.AddHandler(cookie, [] {}));
  EXPECT_TRUE(executor.RemoveHandler(cookie));
  executor.Stop();
}

TEST(PollingThreadExecutorTest, StopDoesNotInterruptAnAlreadyEnteredSleep) {
  ThreadExecutorConfig config;
  config.idle_strategy = IdleStrategy::kIdle;
  config.sleep_us = 100ms;
  PollingThreadExecutor executor(config);
  entered_sleep.store(false);
  release_sleep.store(false);
  watch_sleep.store(true);
  ASSERT_TRUE(executor.Start());
  const bool sleeping = Eventually([] { return entered_sleep.load(); });
  // Release the test hook even if the sleep implementation changes, to avoid
  // blocking the worker during a failed test's destructor.
  if (!sleeping) {
    release_sleep.store(true);
    watch_sleep.store(false);
    FAIL() << "worker did not enter nanosleep";
  }
  auto stopped = std::async(std::launch::async, [&] { executor.Stop(); });
  release_sleep.store(true);
  const auto status = stopped.wait_for(20ms);
  stopped.get();
  EXPECT_EQ(status, std::future_status::timeout);
}
}  // namespace
}  // namespace basic

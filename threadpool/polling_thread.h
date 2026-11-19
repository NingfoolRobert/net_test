#ifndef NET_TEST_THREADPOOL_POLLING_THREAD_H_
#define NET_TEST_THREADPOOL_POLLING_THREAD_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace net_test {

enum class SchedulingPolicy { kOther, kFifo, kRoundRobin };
enum class IdleStrategy { kBusySpin, kYield, kSleep };

struct PollingThreadOptions {
  std::vector<int> cpu_ids;
  SchedulingPolicy scheduling_policy = SchedulingPolicy::kOther;
  int priority = 0;
  std::optional<int> nice_value;
  IdleStrategy idle_strategy = IdleStrategy::kBusySpin;
  std::chrono::microseconds sleep_us{1};
  bool idle_only = true;
  bool collect_task_stats = true;
};

using TaskId = std::uint64_t;

struct TaskOptions {
  std::string name;
  std::string description;
  std::map<std::string, std::string> attributes;
};

struct TaskInfo {
  TaskId id = 0;
  TaskOptions options;
  bool enabled = true;
  std::uint64_t calls = 0;
  std::uint64_t work_count = 0;
  std::uint64_t error_count = 0;
  std::string last_error;
};

struct ThreadInfo {
  PollingThreadOptions options;
  std::vector<int> effective_cpu_ids;
  SchedulingPolicy effective_policy = SchedulingPolicy::kOther;
  int effective_priority = 0;
  int effective_nice = 0;
};

// Owns a single Linux polling thread. Callbacks return whether work was done.
// See polling_thread_design.md for concurrency and shutdown contracts.
class PollingThread {
 public:
  using Callback = std::function<bool()>;

  explicit PollingThread(PollingThreadOptions options = {});
  ~PollingThread();

  PollingThread(const PollingThread&) = delete;
  PollingThread& operator=(const PollingThread&) = delete;
  PollingThread(PollingThread&&) = delete;
  PollingThread& operator=(PollingThread&&) = delete;

  // One startup attempt per instance. Throws on configuration/OS errors.
  void Start();
  TaskId AddTask(TaskOptions options, Callback callback);
  // Waits for any invocation and callback destruction before returning.
  bool RemoveTask(TaskId id);
  std::vector<TaskInfo> ListTasks();
  ThreadInfo GetThreadInfo();

  // May be called from a callback. Does not wait for the worker to exit.
  void RequestStop();
  // Idempotent. Synchronous operations including Stop reject self-waiting.
  void Stop();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace net_test

#endif  // NET_TEST_THREADPOOL_POLLING_THREAD_H_

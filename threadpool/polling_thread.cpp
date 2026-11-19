#include "polling_thread.h"

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <exception>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace net_test {
namespace {

int NativePolicy(SchedulingPolicy policy) {
  switch (policy) {
    case SchedulingPolicy::kOther:
      return SCHED_OTHER;
    case SchedulingPolicy::kFifo:
      return SCHED_FIFO;
    case SchedulingPolicy::kRoundRobin:
      return SCHED_RR;
  }
  throw std::invalid_argument("invalid scheduling policy");
}

void CheckPthread(int error, const char* operation) {
  if (error != 0) {
    throw std::system_error(error, std::generic_category(), operation);
  }
}

void ValidateOptions(PollingThreadOptions& options) {
  const int policy = NativePolicy(options.scheduling_policy);
  if (options.priority < sched_get_priority_min(policy) ||
      options.priority > sched_get_priority_max(policy)) {
    throw std::invalid_argument("priority outside scheduling policy range");
  }
  if (options.nice_value.has_value() &&
      (policy != SCHED_OTHER || *options.nice_value < -20 ||
       *options.nice_value > 19)) {
    throw std::invalid_argument("nice requires OTHER and a value in [-20, 19]");
  }
  switch (options.idle_strategy) {
    case IdleStrategy::kBusySpin:
    case IdleStrategy::kYield:
      break;
    case IdleStrategy::kSleep:
      // Avoid duration overflow when wait_for converts to a finer clock unit.
      if (options.sleep_us.count() <= 0 ||
          options.sleep_us > std::chrono::hours(24)) {
        throw std::invalid_argument("sleep_us must be in (0, 24 hours]");
      }
      break;
    default:
      throw std::invalid_argument("invalid idle strategy");
  }
  std::sort(options.cpu_ids.begin(), options.cpu_ids.end());
  for (std::size_t i = 0; i < options.cpu_ids.size(); ++i) {
    if (options.cpu_ids[i] < 0 || options.cpu_ids[i] >= CPU_SETSIZE ||
        (i > 0 && options.cpu_ids[i] == options.cpu_ids[i - 1])) {
      throw std::invalid_argument("CPU IDs must be distinct and < CPU_SETSIZE");
    }
  }
}

ThreadInfo ConfigureThread(const PollingThreadOptions& options) {
  ThreadInfo info;
  info.options = options;
  const auto self = pthread_self();
  if (!options.cpu_ids.empty()) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (int cpu : options.cpu_ids) {
      CPU_SET(cpu, &mask);
    }
    CheckPthread(pthread_setaffinity_np(self, sizeof(mask), &mask),
                 "pthread_setaffinity_np");
  }
  cpu_set_t effective;
  CPU_ZERO(&effective);
  CheckPthread(pthread_getaffinity_np(self, sizeof(effective), &effective),
               "pthread_getaffinity_np");
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &effective)) {
      info.effective_cpu_ids.push_back(cpu);
    }
  }
  if (!options.cpu_ids.empty() &&
      info.effective_cpu_ids != options.cpu_ids) {
    throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                            "requested CPU affinity was restricted by OS");
  }
  sched_param param{};
  param.sched_priority = options.priority;
  const int policy = NativePolicy(options.scheduling_policy);
  CheckPthread(pthread_setschedparam(self, policy, &param),
               "pthread_setschedparam");
  int effective_policy = 0;
  CheckPthread(pthread_getschedparam(self, &effective_policy, &param),
               "pthread_getschedparam");
  if (effective_policy != policy || param.sched_priority != options.priority) {
    throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                            "requested scheduling parameters did not take effect");
  }
  info.effective_policy = options.scheduling_policy;
  info.effective_priority = param.sched_priority;
  // On Linux, who=0 changes only the calling thread's nice value.
  if (options.nice_value.has_value() &&
      setpriority(PRIO_PROCESS, 0, *options.nice_value) != 0) {
    throw std::system_error(errno, std::generic_category(), "setpriority");
  }
  errno = 0;
  info.effective_nice = getpriority(PRIO_PROCESS, 0);
  if (errno != 0) {
    throw std::system_error(errno, std::generic_category(), "getpriority");
  }
  if (options.nice_value.has_value() &&
      info.effective_nice != *options.nice_value) {
    throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                            "requested nice value did not take effect");
  }
  return info;
}

}  // namespace

class PollingThread::Impl {
 public:
  explicit Impl(PollingThreadOptions options) : options_(std::move(options)) {
    ValidateOptions(options_);
  }

  void Start() {
    RejectSelfWait();
    const std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (start_attempted_ || stop_requested_.load(std::memory_order_acquire)) {
      throw std::system_error(
          std::make_error_code(std::errc::operation_not_permitted),
          "PollingThread cannot be restarted");
    }
    start_attempted_ = true;
    std::promise<void> startup;
    auto ready = startup.get_future();
    worker_ = std::thread([this, startup = std::move(startup)]() mutable {
      current_ = this;
      try {
        thread_info_ = ConfigureThread(options_);
        {
          const std::lock_guard<std::mutex> lock(command_mutex_);
          accepting_ = !stop_requested_.load(std::memory_order_acquire);
        }
        startup.set_value();
      } catch (...) {
        startup.set_exception(std::current_exception());
        current_ = nullptr;
        return;
      }
      Run();
      current_ = nullptr;
    });
    try {
      ready.get();
    } catch (...) {
      worker_.join();
      throw;
    }
  }

  TaskId AddTask(TaskOptions options, Callback callback) {
    if (!callback) {
      throw std::invalid_argument("polling callback must not be empty");
    }
    return Submit([this, options = std::move(options),
                   callback = std::move(callback)]() mutable {
      if (next_id_ == std::numeric_limits<TaskId>::max()) {
        throw std::overflow_error("task ID space exhausted");
      }
      auto task = std::make_unique<Task>();
      task->info.id = next_id_++;
      task->info.options = std::move(options);
      task->callback = std::move(callback);
      const TaskId id = task->info.id;
      tasks_.push_back(std::move(task));
      return id;
    });
  }

  bool RemoveTask(TaskId id) {
    return Submit([this, id] {
      const auto found =
          std::find_if(tasks_.begin(), tasks_.end(),
                       [id](const auto& task) { return task->info.id == id; });
      if (found == tasks_.end()) {
        return false;
      }
      tasks_.erase(found);
      return true;
    });
  }

  std::vector<TaskInfo> ListTasks() {
    return Submit([this] {
      std::vector<TaskInfo> result;
      result.reserve(tasks_.size());
      for (const auto& task : tasks_) {
        result.push_back(task->info);
      }
      return result;
    });
  }

  ThreadInfo GetThreadInfo() {
    return Submit([this] { return thread_info_; });
  }

  void RequestStop() {
    {
      // The waiter uses this mutex too: notifications cannot be lost between
      // evaluating its predicate and entering the wait.
      const std::lock_guard<std::mutex> lock(command_mutex_);
      accepting_ = false;
      stop_requested_.store(true, std::memory_order_release);
    }
    wakeup_.notify_one();
  }

  void Stop() {
    RejectSelfWait();
    const std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    RequestStop();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  struct Task {
    TaskInfo info;
    Callback callback;
  };
  using Command = std::function<void()>;

  void RejectSelfWait() const {
    if (current_ == this) {
      throw std::system_error(
          std::make_error_code(std::errc::resource_deadlock_would_occur),
          "synchronous operation from polling callback");
    }
  }

  template <typename Function>
  auto Submit(Function function) -> std::invoke_result_t<Function> {
    RejectSelfWait();
    using Result = std::invoke_result_t<Function>;
    auto task =
        std::make_shared<std::packaged_task<Result()>>(std::move(function));
    auto done = task->get_future();
    {
      const std::lock_guard<std::mutex> lock(command_mutex_);
      if (!accepting_) {
        throw std::system_error(
            std::make_error_code(std::errc::operation_not_permitted),
            "PollingThread is not accepting commands");
      }
      commands_.emplace_back([task] { (*task)(); });
      pending_.store(true, std::memory_order_release);
    }
    wakeup_.notify_one();
    return done.get();
  }

  void DrainCommands() {
    if (!pending_.load(std::memory_order_acquire)) {
      return;
    }
    {
      const std::lock_guard<std::mutex> lock(command_mutex_);
      batch_.swap(commands_);
      pending_.store(false, std::memory_order_release);
    }
    for (auto& command : batch_) {
      command();  // packaged_task transports exceptions to the caller.
    }
    batch_.clear();
  }

  static void DisableTask(TaskInfo& info, const char* error) noexcept {
    info.enabled = false;
    ++info.error_count;
    try {
      info.last_error = error;
    } catch (...) {
      // Even if diagnostic allocation fails, the faulty task stays disabled.
    }
  }

  bool PollTasks() {
    bool did_work = false;
    for (const auto& task : tasks_) {
      if (stop_requested_.load(std::memory_order_acquire)) {
        break;
      }
      auto& info = task->info;
      if (!info.enabled) {
        continue;
      }
      if (options_.collect_task_stats) {
        ++info.calls;
      }
      try {
        const bool work = task->callback();
        did_work |= work;
        if (options_.collect_task_stats && work) {
          ++info.work_count;
        }
      } catch (const std::exception& error) {
        DisableTask(info, error.what());
      } catch (...) {
        DisableTask(info, "non-standard exception");
      }
    }
    return did_work;
  }

  void Idle(bool did_work) {
    if ((options_.idle_only && did_work) ||
        options_.idle_strategy == IdleStrategy::kBusySpin) {
      return;
    }
    if (options_.idle_strategy == IdleStrategy::kYield) {
      std::this_thread::yield();
      return;
    }
    std::unique_lock<std::mutex> lock(command_mutex_);
    wakeup_.wait_for(lock, options_.sleep_us, [this] {
      return stop_requested_.load(std::memory_order_acquire) ||
             pending_.load(std::memory_order_acquire);
    });
  }

  void Run() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
      DrainCommands();
      Idle(PollTasks());
    }
    // RequestStop closed admission under command_mutex_. All previously
    // accepted operations must complete, including commands in the last batch.
    DrainCommands();
    tasks_.clear();
  }

  static thread_local Impl* current_;
  const PollingThreadOptions options_;
  std::mutex lifecycle_mutex_;
  std::thread worker_;
  bool start_attempted_ = false;
  std::mutex command_mutex_;
  std::condition_variable wakeup_;
  bool accepting_ = false;
  std::vector<Command> commands_;
  std::vector<Command> batch_;
  std::atomic<bool> pending_{false};
  std::atomic<bool> stop_requested_{false};
  std::vector<std::unique_ptr<Task>> tasks_;
  TaskId next_id_ = 1;
  ThreadInfo thread_info_;
};

thread_local PollingThread::Impl* PollingThread::Impl::current_ = nullptr;

PollingThread::PollingThread(PollingThreadOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

PollingThread::~PollingThread() { impl_->Stop(); }
void PollingThread::Start() { impl_->Start(); }
TaskId PollingThread::AddTask(TaskOptions options, Callback callback) {
  return impl_->AddTask(std::move(options), std::move(callback));
}
bool PollingThread::RemoveTask(TaskId id) { return impl_->RemoveTask(id); }
std::vector<TaskInfo> PollingThread::ListTasks() { return impl_->ListTasks(); }
ThreadInfo PollingThread::GetThreadInfo() { return impl_->GetThreadInfo(); }
void PollingThread::RequestStop() { impl_->RequestStop(); }
void PollingThread::Stop() { impl_->Stop(); }

}  // namespace net_test

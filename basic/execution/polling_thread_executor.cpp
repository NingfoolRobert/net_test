#include "polling_thread_executor.h"

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

namespace basic {
namespace {

constexpr std::size_t  kInvalidIndex = std::numeric_limits<std::size_t>::max();
std::atomic<HandlerId> next_handler_id{1};

HandlerId AllocateHandlerId() {
    auto id = next_handler_id.load(std::memory_order_relaxed);
    for (;;) {
        // Reserve the final value as an exhaustion sentinel; never wrap the counter
        // or reuse an ID, including across executor lifetimes.
        if (id == std::numeric_limits<HandlerId>::max()) {
            throw std::overflow_error("handler ID space exhausted");
        }
        if (next_handler_id.compare_exchange_weak(id, id + 1, std::memory_order_relaxed)) {
            return id;
        }
    }
}

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

std::optional<int> NativeNice(NicePriority priority) {
    switch (priority) {
        case NicePriority::kLowest:
            return 19;
        case NicePriority::kLower:
            return 10;
        case NicePriority::kMedium:
            return std::nullopt;
        case NicePriority::kHigher:
            return -10;
        case NicePriority::kHighest:
            return -20;
    }
    throw std::invalid_argument("invalid nice priority");
}

ThreadExecutorConfig ValidateConfig(ThreadExecutorConfig config) {
    const int policy = NativePolicy(config.scheduling_policy);
    if (config.name.size() > 15 || config.name.find('\0') != std::string::npos) {
        throw std::invalid_argument("thread name must contain at most 15 non-NUL bytes");
    }
    if (config.cpu < -1) {
        throw std::invalid_argument("CPU must be -1 or a logical CPU number");
    }
    if (policy == SCHED_OTHER) {
        NativeNice(config.nice_value);
    }
    else {
        switch (config.priority) {
            case RealtimePriority::kLowest:
            case RealtimePriority::kLower:
            case RealtimePriority::kMedium:
            case RealtimePriority::kHigher:
            case RealtimePriority::kHighest:
                break;
            default:
                throw std::invalid_argument("invalid realtime priority");
        }
        const int priority = static_cast<int>(config.priority);
        if (priority < sched_get_priority_min(policy) || priority > sched_get_priority_max(policy)) {
            throw std::invalid_argument("realtime priority outside native range");
        }
    }
    switch (config.idle_strategy) {
        case IdleStrategy::kBusyPolling:
            break;
        case IdleStrategy::kIdle:
            // Bound conversion/addition inside the platform's sleep implementation.
            if (config.sleep_us.count() <= 0 || config.sleep_us > std::chrono::hours(24)) {
                throw std::invalid_argument("sleep_us must be in (0, 24 hours]");
            }
            break;
        default:
            throw std::invalid_argument("invalid idle strategy");
    }
    return config;
}

void CheckPthread(int error, const char *operation) {
    if (error != 0) {
        throw std::system_error(error, std::generic_category(), operation);
    }
}

void ConfigureThread(const ThreadExecutorConfig &config) {
    const auto  self      = pthread_self();
    const char *operation = nullptr;
    if (!config.name.empty()) {
        operation = "pthread_setname_np";
        CheckPthread(pthread_setname_np(self, config.name.c_str()), operation);
    }
    std::array<char, 16> name{};
    operation = "pthread_getname_np";
    CheckPthread(pthread_getname_np(self, name.data(), name.size()), operation);
    if (!config.name.empty() && config.name != name.data()) {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument), "thread name did not take effect");
    }
    if (config.cpu >= 0) {
        operation = "pthread_setaffinity_np";
        if (config.cpu >= CPU_SETSIZE) {
            throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                                    "CPU is not supported by cpu_set_t");
        }
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(config.cpu, &mask);
        CheckPthread(pthread_setaffinity_np(self, sizeof(mask), &mask), operation);
    }
    cpu_set_t effective;
    CPU_ZERO(&effective);
    operation = "pthread_getaffinity_np";
    CheckPthread(pthread_getaffinity_np(self, sizeof(effective), &effective), operation);
    if (config.cpu >= 0 && (CPU_COUNT(&effective) != 1 || !CPU_ISSET(config.cpu, &effective))) {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                                "requested affinity did not take effect");
    }
    const int   policy = NativePolicy(config.scheduling_policy);
    sched_param param{};
    param.sched_priority = policy == SCHED_OTHER ? 0 : static_cast<int>(config.priority);
    operation            = "pthread_setschedparam";
    CheckPthread(pthread_setschedparam(self, policy, &param), operation);
    int effective_policy = 0;
    operation            = "pthread_getschedparam";
    CheckPthread(pthread_getschedparam(self, &effective_policy, &param), operation);
    const int expected_priority = param.sched_priority;
    if (effective_policy != policy ||
        expected_priority != (policy == SCHED_OTHER ? 0 : static_cast<int>(config.priority))) {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                                "requested scheduling parameters did not take effect");
    }
    std::optional<int> nice;
    if (policy == SCHED_OTHER) {
        nice = NativeNice(config.nice_value);
    }
    if (nice.has_value()) {
        operation = "setpriority";
        if (setpriority(PRIO_PROCESS, 0, *nice) != 0) {
            throw std::system_error(errno, std::generic_category(), operation);
        }
    }
    operation                = "getpriority";
    errno                    = 0;
    const int effective_nice = getpriority(PRIO_PROCESS, 0);
    if (errno != 0) {
        throw std::system_error(errno, std::generic_category(), operation);
    }
    if (nice.has_value() && effective_nice != *nice) {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                                "requested nice value did not take effect");
    }
}

}  // namespace

thread_local PollingThreadExecutor *PollingThreadExecutor::current_ = nullptr;

PollingThreadExecutor::PollingThreadExecutor(ThreadExecutorConfig config) : config_(ValidateConfig(std::move(config))) {
    commands_.reserve(kMaxPendingCommands);
    batch_.reserve(kMaxPendingCommands);
    // Keep short fallback messages available when error reporting runs out of memory.
    error_.reserve(64);
}

PollingThreadExecutor::~PollingThreadExecutor() {
    try {
        Stop();
    }
    catch (...) {
        // Destruction on the worker is forbidden. A failed join cannot leave a
        // live thread using reclaimed storage; destruction cannot report failure.
        std::terminate();
    }
}

bool PollingThreadExecutor::Start() {
    // A callback may repeat Start while an external Stop holds lifecycle_mutex_.
    if (current_ == this) {
        if (stop_requested_.load(std::memory_order_acquire)) {
            if (error_.empty()) {
                error_ = "executor has been requested to stop";
            }
            return false;
        }
        error_.clear();
        return true;
    }
    const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (stop_requested_.load(std::memory_order_acquire)) {
        if (error_.empty()) {
            error_ = "executor has been requested to stop";
        }
        return false;
    }
    if (running_.load(std::memory_order_acquire)) {
        error_.clear();
        return true;
    }
    error_.clear();
    try {
        std::promise<bool> startup;
        auto               ready = startup.get_future();
        worker_                  = std::thread([this, startup = std::move(startup)]() mutable {
            current_     = this;
            bool started = false;
            try {
                ConfigureThread(config_);
                {
                    const std::lock_guard<SpinLock> command_lock(command_lock_);
                    if (!stop_requested_.load(std::memory_order_acquire)) {
                        accepting_ = true;
                        running_.store(true, std::memory_order_release);
                        started = true;
                    }
                }
                if (!started) {
                    error_ = "stop requested during startup";
                }
            }
            catch (const std::exception &error) {
                error_ = "startup failed";
                try {
                    if (*error.what() != '\0') {
                        error_ = error.what();
                    }
                }
                catch (...) {
                    // Keep the fallback if copying the diagnostic fails.
                }
            }
            catch (...) {
                error_ = "non-standard startup exception";
            }
            startup.set_value(started);
            if (started) {
                Run();
            }
            current_ = nullptr;
        });
        if (ready.get()) {
            return true;
        }
        worker_.join();
        return false;
    }
    catch (const std::exception &error) {
        error_ = "startup failed";
        try {
            if (*error.what() != '\0') {
                error_ = error.what();
            }
        }
        catch (...) {
            // Keep the fallback if copying the diagnostic fails.
        }
    }
    return false;
}

std::string PollingThreadExecutor::GetError() const {
    return error_;
}

bool PollingThreadExecutor::AddHandler(HandlerCookie *&cookie, Handler handler, std::string name, bool async) {
    if (!handler) {
        return false;
    }
    Command        command;
    HandlerCookie *output = nullptr;
    try {
        command.entry.cookie             = std::make_shared<HandlerCookie>();
        command.entry.cookie->handler_id = AllocateHandlerId();
        command.entry.cookie->owner      = this;
        output                           = command.entry.cookie.get();
        command.entry.callback           = std::move(handler);
        command.entry.name               = std::move(name);
    }
    catch (...) {
        return false;
    }
    if (!async && current_ == this) {
        {
            const std::lock_guard<SpinLock> lock(command_lock_);
            if (!accepting_ || reserved_handlers_ == kMaxHandlers) {
                return false;
            }
            ++reserved_handlers_;
        }
        assert(handler_count_ < kMaxHandlers);
        command.entry.cookie->idx   = handler_count_;
        handlers_[handler_count_++] = std::move(command.entry);
        cookie                      = output;
        return true;
    }
    if (!SubmitCommand(std::move(command), async)) {
        return false;
    }
    cookie = output;
    return true;
}

bool PollingThreadExecutor::RemoveHandler(HandlerCookie *&cookie, bool async) {
    if (cookie == nullptr) {
        return false;
    }
    if (!async && current_ == this) {
        {
            const std::lock_guard<SpinLock> lock(command_lock_);
            if (!accepting_) {
                return false;
            }
        }
        if (cookie->owner != this) {
            return false;
        }
        if (cookie->idx == kInvalidIndex) {
            cookie->cancel_pending_add = true;
        }
        else {
            if (cookie->idx >= handler_count_ || handlers_[cookie->idx].cookie.get() != cookie ||
                handlers_[cookie->idx].pending_remove) {
                return false;
            }
            handlers_[cookie->idx].pending_remove = true;
            has_pending_removals_                 = true;
        }
        cookie->owner = nullptr;
        cookie->idx   = kInvalidIndex;
        cookie        = nullptr;
        return true;
    }
    Command command;
    command.kind   = CommandKind::kDel;
    command.cookie = cookie;
    if (!SubmitCommand(std::move(command), async)) {
        return false;
    }
    cookie = nullptr;
    return true;
}

void PollingThreadExecutor::Stop() {
    if (current_ == this) {
        throw std::logic_error("Stop cannot wait for its own polling thread");
    }
    {
        const std::lock_guard<SpinLock> lock(command_lock_);
        accepting_ = false;
        stop_requested_.store(true, std::memory_order_release);
    }
    const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool PollingThreadExecutor::SubmitCommand(Command command, bool async) {
    // Prepare before enqueueing: the worker may finish the command immediately.
    if (!async) {
        try {
            command.sync_id = sync_pair_.fetch_id();
            if (!sync_pair_.prepare(command.sync_id)) {
                return false;
            }
        }
        catch (...) {
            return false;
        }
    }
    const auto sync_id  = command.sync_id;
    const bool add      = command.kind == CommandKind::kAdd;
    bool       accepted = false;
    {
        const std::lock_guard<SpinLock> lock(command_lock_);
        if (accepting_ && pending_commands_ < kMaxPendingCommands && (!add || reserved_handlers_ < kMaxHandlers)) {
            commands_.push_back(std::move(command));
            ++pending_commands_;
            if (add) {
                ++reserved_handlers_;
            }
            pending_.store(true, std::memory_order_release);
            accepted = true;
        }
    }
    // Cancel or wait outside the queue lock so the worker can make progress.
    if (!accepted) {
        if (!async) {
            const bool canceled = sync_pair_.cancel(sync_id);
            assert(canceled);
            (void)canceled;
        }
        return false;
    }
    if (async) {
        return true;
    }
    bool       result   = false;
    const bool received = sync_pair_.wait(result, sync_id);
    assert(received);
    return received && result;
}

void PollingThreadExecutor::CleanupPendingRemovals() {
    if (!has_pending_removals_) {
        return;
    }
    for (std::size_t i = 0; i < handler_count_;) {
        if (handlers_[i].pending_remove) {
            auto removed_cookie = handlers_[i].cookie;
            {
                HandlerEntry      removed = std::move(handlers_[i]);
                const std::size_t last    = handler_count_ - 1;
                if (i != last) {
                    handlers_[i] = std::move(handlers_[last]);
                    if (!handlers_[i].pending_remove) {
                        handlers_[i].cookie->idx = i;
                    }
                }
                handlers_[last] = HandlerEntry{};
                --handler_count_;
                removed_cookie->owner = nullptr;
                removed_cookie->idx   = kInvalidIndex;
                // Restore storage before destroying captures, then release the slot.
            }
            const std::lock_guard<SpinLock> lock(command_lock_);
            assert(reserved_handlers_ > 0);
            --reserved_handlers_;
        }
        else {
            ++i;
        }
    }
    has_pending_removals_ = false;
}

void PollingThreadExecutor::Execute(Command command) {
    bool                           result = false;
    std::shared_ptr<HandlerCookie> removed_cookie;
    switch (command.kind) {
        case CommandKind::kAdd:
            if (!command.entry.cookie->cancel_pending_add) {
                assert(handler_count_ < kMaxHandlers);
                command.entry.cookie->idx   = handler_count_;
                handlers_[handler_count_++] = std::move(command.entry);
                result                      = true;
            }
            else {
                command.entry.cookie->owner = nullptr;
                command.entry.cookie->idx   = kInvalidIndex;
                command.entry               = HandlerEntry{};
                const std::lock_guard<SpinLock> lock(command_lock_);
                assert(reserved_handlers_ > 0);
                --reserved_handlers_;
            }
            break;
        case CommandKind::kDel:
            if (command.cookie->owner == this && command.cookie->idx < handler_count_ &&
                handlers_[command.cookie->idx].cookie.get() == command.cookie &&
                !handlers_[command.cookie->idx].pending_remove) {
                const std::size_t index = command.cookie->idx;
                removed_cookie          = handlers_[index].cookie;
                {
                    HandlerEntry      removed = std::move(handlers_[index]);
                    const std::size_t last    = handler_count_ - 1;
                    if (index != last) {
                        handlers_[index] = std::move(handlers_[last]);
                        if (!handlers_[index].pending_remove) {
                            handlers_[index].cookie->idx = index;
                        }
                    }
                    handlers_[last] = HandlerEntry{};
                    --handler_count_;
                    removed_cookie->owner = nullptr;
                    removed_cookie->idx   = kInvalidIndex;
                    // Reclaim captures before notifying a synchronous caller.
                }
                {
                    const std::lock_guard<SpinLock> lock(command_lock_);
                    assert(reserved_handlers_ > 0);
                    --reserved_handlers_;
                }
                result = true;
            }
            break;
    }
    {
        const std::lock_guard<SpinLock> lock(command_lock_);
        assert(pending_commands_ > 0);
        --pending_commands_;
    }
    if (command.sync_id != 0) {
        const bool notified = sync_pair_.notify(command.sync_id, result);
        assert(notified);
        (void)notified;
    }
}

void PollingThreadExecutor::DrainCommands(std::size_t budget) {
    for (std::size_t processed = 0; processed < budget; ++processed) {
        if (batch_idx == batch_.size()) {
            batch_.clear();
            batch_idx = 0;
            if (!pending_.load(std::memory_order_acquire)) {
                return;
            }
            const std::lock_guard<SpinLock> lock(command_lock_);
            batch_.swap(commands_);
            pending_.store(false, std::memory_order_release);
            if (batch_.empty()) {
                return;
            }
        }
        Execute(std::move(batch_[batch_idx++]));
    }
}
void PollingThreadExecutor::PollHandlers() {
    const std::size_t round_count = handler_count_;
    for (std::size_t i = 0; i < round_count; ++i) {
        if (stop_requested_.load(std::memory_order_acquire)) {
            break;
        }
        auto &entry = handlers_[i];
        if (entry.pending_remove) {
            continue;
        }
        try {
            entry.callback();
        }
        catch (...) {
            if (!has_runtime_error_) {
                has_runtime_error_ = true;
                const char *message = "non-standard handler exception";
                try {
                    throw;
                }
                catch (const std::exception &error) {
                    message = error.what();
                }
                catch (...) {
                    // Non-standard exceptions have no what() text.
                }
                error_ = "handler exception";
                try {
                    if (*message != '\0') {
                        error_ = message;
                    }
                }
                catch (...) {
                    // Keep the fallback if copying the diagnostic fails.
                }
            }
            const std::lock_guard<SpinLock> lock(command_lock_);
            accepting_ = false;
            stop_requested_.store(true, std::memory_order_release);
            break;
        }
    }
    CleanupPendingRemovals();
}
void PollingThreadExecutor::Run() {
    try {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            DrainCommands(kCommandsPerRound);
            PollHandlers();
            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }
            if (config_.idle_strategy == IdleStrategy::kBusyPolling) {
                _mm_pause();
            }
            else {
                std::this_thread::sleep_for(config_.sleep_us);
            }
        }
    }
    catch (...) {
        if (!has_runtime_error_) {
            has_runtime_error_ = true;
            const char *message = "non-standard worker exception";
            try {
                throw;
            }
            catch (const std::exception &error) {
                message = error.what();
            }
            catch (...) {
                // Non-standard exceptions have no what() text.
            }
            error_ = "worker exception";
            try {
                if (*message != '\0') {
                    error_ = message;
                }
            }
            catch (...) {
                // Keep the fallback if copying the diagnostic fails.
            }
        }
        const std::lock_guard<SpinLock> lock(command_lock_);
        accepting_ = false;
        stop_requested_.store(true, std::memory_order_release);
    }
    CleanupPendingRemovals();
    DrainCommands(std::numeric_limits<std::size_t>::max());
    while (handler_count_ != 0) {
        const std::size_t last           = handler_count_ - 1;
        auto              removed_cookie = handlers_[last].cookie;
        {
            HandlerEntry removed = std::move(handlers_[last]);
            handlers_[last]      = HandlerEntry{};
            --handler_count_;
            removed_cookie->owner = nullptr;
            removed_cookie->idx   = kInvalidIndex;
            // Tail removal needs no compaction; reclaim captures before the slot.
        }
        const std::lock_guard<SpinLock> lock(command_lock_);
        assert(reserved_handlers_ > 0);
        --reserved_handlers_;
    }
    running_.store(false, std::memory_order_release);
}

}  // namespace basic

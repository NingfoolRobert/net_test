/**
 * @file polling_thread_executor.h
 * @author nbf(nbf0537@163.com)
 * @brief
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "../concurrency/spin_lock.h"
#include "sync_pair.h"

namespace basic {

enum class SchedulingPolicy : uint8_t
{
    kOther,
    kFifo,
    kRoundRobin
};
enum class IdleStrategy : uint8_t
{
    kBusyPolling,
    kIdle
};
enum class RealtimePriority : uint8_t
{
    kLowest = 1,
    kLower = 30,
    kMedium = 50,
    kHigher = 70,
    kHighest = 99
};
enum class NicePriority : uint8_t
{
    kLowest,
    kLower,
    kMedium,
    kHigher,
    kHighest
};
// NOLINTEND(performance-enum-size)

struct ThreadExecutorConfig {
    std::string name;
    int cpu = -1;
    SchedulingPolicy scheduling_policy = SchedulingPolicy::kOther;
    RealtimePriority priority = RealtimePriority::kMedium;
    NicePriority nice_value = NicePriority::kMedium;
    IdleStrategy idle_strategy = IdleStrategy::kBusyPolling;
    std::chrono::microseconds sleep_us{100};
};

using HandlerId = std::uint64_t;
using Handler = std::function<void()>;
class PollingThreadExecutor;

// Non-owning public handle. Only the executor can inspect or modify its fields.
// A deleted/stopped handle must not be reused, including through pointer
// copies.
struct HandlerCookie {
private:
    friend class PollingThreadExecutor;
    HandlerId handler_id = 0;
    std::size_t idx = std::numeric_limits<std::size_t>::max();
    PollingThreadExecutor *owner = nullptr;
    bool cancel_pending_add = false;
};

// C++17/Linux single-worker poller. See README.md for ownership,
// command ordering, sleep, and the distinction between
// unregistering/reclaiming.
class PollingThreadExecutor {
public:
    static constexpr std::size_t kMaxHandlers = 512;
    static constexpr std::size_t kMaxPendingCommands = 1024;
    static constexpr std::size_t kCommandsPerRound = 64;

    explicit PollingThreadExecutor(ThreadExecutorConfig config = {});
    ~PollingThreadExecutor();
    PollingThreadExecutor(const PollingThreadExecutor &) = delete;
    PollingThreadExecutor &operator=(const PollingThreadExecutor &) = delete;
    PollingThreadExecutor(PollingThreadExecutor &&) = delete;
    PollingThreadExecutor &operator=(PollingThreadExecutor &&) = delete;

    // Configuration errors throw at construction. Startup failures return false.
    // Running Start is idempotent; failed startup can retry until stop is
    // requested.
    // Error diagnostics are unsynchronized; callers must prevent concurrent
    // diagnostic access from Start(), GetError(), and worker exception handling.
    bool Start();
    // Error text; empty means no error. Startup retries clear it, while a
    // rejected restart after stopping preserves the original failure.
    // Call only when no thread is updating diagnostics, e.g. after Stop().
    std::string GetError() const;

    // Failure preserves cookie. Success overwrites it without removing its old
    // handler. Only synchronous calls check for execution on this worker.
    bool AddHandler(HandlerCookie *&cookie, Handler handler, std::string name = {}, bool async = false);
    // External synchronous success includes resource reclamation. Worker-local
    // synchronous success unregisters immediately and reclaims at the round end
    // (or when a canceled pending add is consumed). Async always means admission.
    bool RemoveHandler(HandlerCookie *&cookie, bool async = false);

    // Idempotent join. Throws logic_error on this worker, before acquiring a lock
    // that an external Stop might hold while waiting for this worker.
    void Stop();

private:
    struct HandlerEntry {
        std::shared_ptr<HandlerCookie> cookie;
        Handler callback;
        std::string name;
        bool pending_remove = false;
    };
    enum class CommandKind : std::uint8_t
    {
        kAdd,
        kDel
    };
    struct Command {
        CommandKind kind = CommandKind::kAdd;
        HandlerEntry entry;
        HandlerCookie *cookie = nullptr;
        int64_t sync_id = 0;
    };
    static_assert(std::is_nothrow_move_assignable_v<HandlerEntry>);
    static_assert(std::is_nothrow_move_constructible_v<Command>);

    bool SubmitCommand(Command command, bool async);
    void CleanupPendingRemovals();
    void Execute(Command command);
    void DrainCommands(std::size_t budget);
    void PollHandlers();
    void Run();

private:
    static thread_local PollingThreadExecutor *current_;

    ThreadExecutorConfig config_;
    std::mutex lifecycle_mutex_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::string error_;
    // Worker-owned; stop/restart diagnostics do not count as runtime failures.
    bool has_runtime_error_ = false;
    SpinLock command_lock_;
    bool accepting_ = false;
    std::size_t reserved_handlers_ = 0;
    std::size_t pending_commands_ = 0;
    std::vector<Command> commands_;
    std::vector<Command> batch_;
    std::size_t batch_idx = 0;
    std::atomic<bool> pending_{false};
    basic::SyncPair<bool> sync_pair_{1};
    std::vector<HandlerEntry> handlers_{kMaxHandlers};
    std::size_t handler_count_ = 0;
    bool has_pending_removals_ = false;
};

using executor_t = PollingThreadExecutor;
using executor_cookie_t = HandlerCookie;
}  // namespace basic

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace basic {

namespace time_wheel_detail {
// elapsed is nonnegative and tick_ms is a validated, positive period.
inline uint64_t first_expiry_tick(uint64_t epoch_tick, std::chrono::steady_clock::duration elapsed,
                                  uint64_t delay_ms, uint64_t tick_ms) {
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    // Split the unsigned delay before adding it to elapsed time: even a delay
    // larger than the clock's duration range can have a representable tick.
    const uint64_t whole_delay_ticks = delay_ms / tick_ms;
    const uint64_t partial_ms = static_cast<uint64_t>(elapsed_ms.count()) + delay_ms % tick_ms;
    uint64_t offset = partial_ms / tick_ms;
    if (delay_ms == 0 || partial_ms % tick_ms != 0 || elapsed > elapsed_ms) ++offset;

    const uint64_t room = std::numeric_limits<uint64_t>::max() - epoch_tick;
    if (offset > room || whole_delay_ticks > room - offset)
        throw std::overflow_error("Timing wheel counter overflow");
    return epoch_tick + offset + whole_delay_ticks;
}
}  // namespace time_wheel_detail

// Hierarchical timer wheel. Callbacks execute serially on the worker, outside
// the wheel lock. They may add/cancel timers or request stop. External stop()
// waits for the worker, including its current callback, and clears all timers.
// Destroy the wheel from outside its callback; callbacks should be brief.
class TimeWheel {
    using Clock = std::chrono::steady_clock;
public:
    using Callback = std::function<void()>;
    struct Timer {
        uint64_t id, expire_tick, interval_ticks;
        bool repeat;
        Callback cb;
        std::atomic<bool> cancelled{false};
    };
    explicit TimeWheel(std::vector<std::size_t> slots_per_level = {256, 64, 64, 64},
                       uint64_t tick_ms = 100)
        : slots_(std::move(slots_per_level)), tick_ms_(tick_ms) {
        if (slots_.empty() || tick_ms_ == 0 ||
            tick_ms_ > static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::duration::max()).count()))
            throw std::invalid_argument("Invalid timing wheel configuration");
        uint64_t span = 1;
        for (auto count : slots_) {
            if (!count || span > std::numeric_limits<uint64_t>::max() / count)
                throw std::invalid_argument("Invalid timing wheel capacity");
            span_.push_back(span);
            wheels_.emplace_back(count);
            span *= count;
        }
    }
    ~TimeWheel() { stop(); }
    TimeWheel(const TimeWheel &) = delete;
    TimeWheel &operator=(const TimeWheel &) = delete;

    void start() {
        if (active_wheel_ == this) throw std::logic_error("Cannot restart wheel from its callback");
        std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        if (running_.load()) return;
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        epoch_time_ = Clock::now();
        epoch_tick_ = current_tick_;
        running_.store(true);
        try {
            worker_ = std::thread([this, epoch = epoch_time_] { run(epoch); });
        } catch (...) {
            running_.store(false);
            throw;
        }
    }
    void stop() {
        // Self-stop requests termination; an external stop/destructor joins.
        if (active_wheel_ == this) {
            request_stop();
            return;
        }
        std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        request_stop();
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &entry : timers_) entry.second->cancelled.store(true);
        timers_.clear();
        for (auto &level : wheels_) for (auto &slot : level) slot.clear();
        current_tick_ = 0;
    }
    uint64_t add_timer(uint64_t delay_ms, Callback cb, bool repeat = false,
                       uint64_t repeat_interval_ms = 0) {
        if (!cb) return 0;
        if (repeat && repeat_interval_ms == 0)
            throw std::invalid_argument("Repeating timer interval must be positive");
        auto timer = std::make_shared<Timer>();
        timer->repeat = repeat;
        timer->interval_ticks = repeat ? to_ticks(repeat_interval_ms) : 0;
        timer->cb = std::move(cb);
        std::lock_guard<std::mutex> lock(mutex_);
        if (next_id_ == 0)
            throw std::overflow_error("Timing wheel counter overflow");
        const bool running = running_.load();
        timer->expire_tick = time_wheel_detail::first_expiry_tick(
            running ? epoch_tick_ : current_tick_,
            running ? Clock::now() - epoch_time_ : Clock::duration::zero(), delay_ms, tick_ms_);
        timer->id = next_id_++;
        timers_.emplace(timer->id, timer);
        try { insert_locked(timer); }
        catch (...) { timers_.erase(timer->id); throw; }
        return timer->id;
    }
    void cancel_timer(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = timers_.find(id);
        if (it == timers_.end()) return;
        it->second->cancelled.store(true);
        timers_.erase(it);
    }
private:
    void request_stop() {
        {
            std::lock_guard<std::mutex> lock(wait_mutex_);
            running_.store(false);
        }
        wake_.notify_all();
    }
    uint64_t to_ticks(uint64_t ms) const noexcept {
        const auto ticks = ms / tick_ms_ + (ms % tick_ms_ != 0);
        return ticks ? ticks : 1;
    }
    void insert_locked(const std::shared_ptr<Timer> &timer) {
        const uint64_t remaining = timer->expire_tick > current_tick_ ? timer->expire_tick - current_tick_ : 0;
        std::size_t level = 0;
        while (level + 1 < slots_.size() && remaining >= span_[level] * slots_[level]) ++level;
        const auto slot = (timer->expire_tick / span_[level]) % slots_[level];
        wheels_[level][slot].push_back(timer);
    }
    void tick_once() {
        std::list<std::shared_ptr<Timer>> due;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (current_tick_ == std::numeric_limits<uint64_t>::max()) {
                running_.store(false);
                return;
            }
            ++current_tick_;
            // Cascade high levels first, before processing level zero. This
            // includes timers exactly on a lower wheel's wrap boundary.
            for (std::size_t level = wheels_.size(); level-- > 1;) {
                if (current_tick_ % span_[level] != 0) continue;
                const auto slot = (current_tick_ / span_[level]) % slots_[level];
                auto bucket = std::move(wheels_[level][slot]);
                for (const auto &timer : bucket)
                    if (!timer->cancelled.load()) insert_locked(timer);
            }
            const auto slot = current_tick_ % slots_[0];
            auto bucket = std::move(wheels_[0][slot]);
            for (const auto &timer : bucket) {
                if (timer->cancelled.load()) continue;
                if (timer->expire_tick <= current_tick_) due.push_back(timer);
                else insert_locked(timer);
            }
        }
        for (const auto &timer : due) {
            if (running_.load() && !timer->cancelled.load()) {
                try { timer->cb(); }
                catch (...) { /* A failed callback must not terminate the worker. */ }
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if (timer->repeat && running_.load() && !timer->cancelled.load() &&
                timer->interval_ticks <= std::numeric_limits<uint64_t>::max() - current_tick_) {
                timer->expire_tick = current_tick_ + timer->interval_ticks;
                insert_locked(timer);
            } else timers_.erase(timer->id);
        }
    }
    void run(Clock::time_point deadline) noexcept {
        active_wheel_ = this;
        try {
            const auto period = std::chrono::milliseconds(tick_ms_);
            while (running_.load()) {
                if (period > std::chrono::steady_clock::time_point::max() - deadline) {
                    running_.store(false);
                    break;
                }
                deadline += period;
                {
                    std::unique_lock<std::mutex> lock(wait_mutex_);
                    if (wake_.wait_until(lock, deadline, [this] { return !running_.load(); })) break;
                }
                tick_once();
            }
        } catch (...) { running_.store(false); }
        active_wheel_ = nullptr;
    }
    std::vector<std::size_t> slots_;
    uint64_t tick_ms_;
    std::vector<uint64_t> span_;
    std::vector<std::vector<std::list<std::shared_ptr<Timer>>>> wheels_;
    std::mutex mutex_, lifecycle_mutex_, wait_mutex_;
    std::condition_variable wake_;
    std::unordered_map<uint64_t, std::shared_ptr<Timer>> timers_;
    uint64_t current_tick_ = 0, next_id_ = 1;
    Clock::time_point epoch_time_{};
    uint64_t epoch_tick_ = 0;
    std::atomic<bool> running_{false};
    std::thread worker_;
    inline static thread_local TimeWheel *active_wheel_ = nullptr;
};
}  // namespace basic

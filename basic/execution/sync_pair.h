/**
 * @file sync_pair.h
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
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace basic {

// One notification and one consumer per ID. IDs must not be reused. Each
// participant pins the record, so consuming a result cannot destroy a notifier
// still returning from notify(). Timed-out records remain available.
template <typename T>
class SyncPair {
public:
    explicit SyncPair(int64_t seq_id) : seq_id_(seq_id) {
        if (seq_id < 0) {
            throw std::invalid_argument("negative synchronization sequence");
        }
    }
    SyncPair(const SyncPair &)            = delete;
    SyncPair &operator=(const SyncPair &) = delete;

    SyncPair(SyncPair &&)            = delete;
    SyncPair &operator=(SyncPair &&) = delete;
    ~SyncPair()                      = default;

    int64_t fetch_id() {
        auto id = seq_id_.load(std::memory_order_relaxed);
        for (;;) {
            if (id == std::numeric_limits<int64_t>::max()) {
                throw std::overflow_error("synchronization ID space exhausted");
            }
            if (seq_id_.compare_exchange_weak(id, id + 1, std::memory_order_relaxed)) {
                return id;
            }
        }
    }

    // Prepare before exposing work to a notifier. Failed admission must cancel
    // the record. reset() preserves prepared records and unconsumed results.
    bool prepare(int64_t id) {
        auto state      = std::make_shared<State>();
        state->prepared = true;
        const std::lock_guard<std::mutex> lock(mutex_);
        return states_.emplace(id, std::move(state)).second;
    }

    bool cancel(int64_t id) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto                        found = states_.find(id);
        if (found == states_.end() || found->second.use_count() != 1) {
            return false;
        }
        // No participant can acquire the record while mutex_ is held.
        {
            const std::lock_guard<std::mutex> state_lock(found->second->mutex);
            if (found->second->result.has_value()) {
                return false;
            }
        }
        states_.erase(found);
        return true;
    }

    bool notify(int64_t id, const T &data) {
        const auto state = GetOrCreate(id);
        {
            const std::lock_guard<std::mutex> lock(state->mutex);
            if (state->result.has_value() || state->consumed) {
                return false;
            }
            state->result.emplace(data);
        }
        state->ready.notify_one();
        return true;
    }

    bool wait(T &data, int64_t id) {
        return Wait(data, id, [](State &state, auto &lock) {
            state.ready.wait(lock, [&state] { return state.result.has_value(); });
            return true;
        });
    }

    bool wait_for(T &data, int64_t id, std::chrono::milliseconds timeout) {
        return Wait(data, id, [timeout](State &state, auto &lock) {
            return state.ready.wait_for(lock, timeout, [&state] { return state.result.has_value(); });
        });
    }

    bool wait_until(T &data, int64_t id, std::chrono::steady_clock::time_point deadline) {
        return Wait(data, id, [deadline](State &state, auto &lock) {
            return state.ready.wait_until(lock, deadline, [&state] { return state.result.has_value(); });
        });
    }

    void reset() {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = states_.begin(); it != states_.end();) {
            // With no pins and mutex_ held, no participant can access this record.
            bool removable = false;
            if (it->second.use_count() == 1) {
                const std::lock_guard<std::mutex> state_lock(it->second->mutex);
                removable = !it->second->prepared && !it->second->result.has_value();
            }
            if (removable) {
                it = states_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    std::size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return states_.size();
    }

private:
    struct State {
        std::mutex              mutex;
        std::condition_variable ready;
        std::optional<T>        result;
        bool                    prepared = false;
        bool                    waiting  = false;
        bool                    consumed = false;
    };

    std::shared_ptr<State> GetOrCreate(int64_t id) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto                        found = states_.find(id);
        if (found != states_.end()) {
            return found->second;
        }
        auto state = std::make_shared<State>();
        states_.emplace(id, state);
        return state;
    }

    template <typename Waiter>
    bool Wait(T &data, int64_t id, Waiter waiter) {
        const auto state = GetOrCreate(id);
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            if (state->waiting || state->consumed) {
                return false;
            }
            state->waiting = true;
            try {
                if (!waiter(*state, lock) || !state->result.has_value()) {
                    state->waiting = false;
                    return false;
                }
                data            = state->result.value();
                state->consumed = true;
                state->waiting  = false;
            }
            catch (...) {
                state->waiting = false;
                throw;
            }
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        states_.erase(id);
        return true;
    }

    mutable std::mutex                                  mutex_;
    std::unordered_map<int64_t, std::shared_ptr<State>> states_;
    std::atomic<int64_t>                                seq_id_;
};

}  // namespace basic

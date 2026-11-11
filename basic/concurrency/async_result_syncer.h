/**
 * @file thread_synchronizer.h
 * @brief
 * @author nbf
 * @date 2026-01-05
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace detail {

// Each ID identifies one result and one waiting consumer. A timed-out
// wait may be retried with the same ID. Reset and moves require quiescence.
template <typename T>
class SyncPair {
  public:
  explicit SyncPair(int64_t seqId) : seqId_(seqId) {
  }

  ~SyncPair() = default;

  // Copy constructor
  SyncPair(const SyncPair &other) = delete;
  // Copy assignment operator
  SyncPair &operator=(const SyncPair &other) = delete;
  // Move constructor
  SyncPair(SyncPair &&other) noexcept : sync_data_map_(std::move(other.sync_data_map_)), seqId_(other.seqId_.load()) {
  }
  // Move assignment operator
  SyncPair &operator=(SyncPair &&other) noexcept {
    if (this != &other) {
      sync_data_map_ = std::move(other.sync_data_map_);
      seqId_.store(other.seqId_.load());
    }
    return *this;
  }

  void notify(int64_t id, T &data) {
    const auto state = get_sync_data(id);
    const std::lock_guard<std::mutex> lock(state->notify_mutex);
    if (!state->notified) {
      state->setter.set_value(data);
      state->notified = true;
    }
  }

  bool wait(T &data, int64_t id) {
    const auto state = get_sync_data(id);
    state->getter.wait();
    return consume(data, id, state);
  }

  bool wait_for(T &data, int64_t id, std::chrono::milliseconds timeout) {
    const auto state = get_sync_data(id);
    if (state->getter.wait_for(timeout) != std::future_status::ready) {
      return false;
    }
    return consume(data, id, state);
  }

  bool wait_until(T &data, int64_t id, std::chrono::steady_clock::time_point deadline) {
    const auto state = get_sync_data(id);
    if (state->getter.wait_until(deadline) != std::future_status::ready) {
      return false;
    }
    return consume(data, id, state);
  }

  void reset() {
    const std::lock_guard<std::mutex> lock(lck_);
    sync_data_map_.clear();
  }

  int64_t fetch_id() {
    return seqId_++;
  }

  private:
  struct SyncData {
    std::promise<T> setter;
    std::future<T> getter = setter.get_future();
    std::mutex notify_mutex;
    bool notified = false;
  };

  std::shared_ptr<SyncData> get_sync_data(int64_t id) {
    const std::lock_guard<std::mutex> lock(lck_);
    const auto it = sync_data_map_.find(id);
    if (it != sync_data_map_.end()) {
      return it->second;
    }
    auto state = std::make_shared<SyncData>();
    sync_data_map_.emplace(id, state);
    return state;
  }

  bool consume(T &data, int64_t id, const std::shared_ptr<SyncData> &state) {
    {
      const std::lock_guard<std::mutex> lock(lck_);
      sync_data_map_.erase(id);
    }
    // The waiter and any notifier retain ownership after erasing the entry.
    data = state->getter.get();
    return true;
  }

  std::mutex lck_;
  std::unordered_map<int64_t, std::shared_ptr<SyncData>> sync_data_map_;
  std::atomic_int64_t seqId_;
};

template <typename T>
class AsyncResultSyncer {
  public:
  explicit AsyncResultSyncer(std::function<bool(T &)> predicate, int64_t seqId = 0)
      : sync_pair_(seqId), predicate_(std::move(predicate)) {
  }

  ~AsyncResultSyncer() {
  }
  // Copy constructor
  AsyncResultSyncer(const AsyncResultSyncer &other) = delete;

  // Copy assignment operator
  AsyncResultSyncer &operator=(const AsyncResultSyncer &other) {
    if (this != &other) {
      sync_pair_ = other.sync_pair_;
      predicate_ = other.predicate_;
      seqId_ = other.seqId_.load();
    }
    return *this;
  }
  // Move constructor
  AsyncResultSyncer(AsyncResultSyncer &&other) noexcept
      : sync_pair_(std::move(other.sync_pair_)), predicate_(std::move(other.predicate_)), seqId_(other.seqId_.load()) {
  }

  // Move assignment operator
  AsyncResultSyncer &operator=(AsyncResultSyncer &&other) noexcept {
    if (this != &other) {
      sync_pair_ = std::move(other.sync_pair_);
      predicate_ = std::move(other.predicate_);
      seqId_ = other.seqId_.load();
    }
    return *this;
  }

  public:
  bool wait(T &data, int64_t id) {
    return sync_pair_.wait(data, id) && predicate_(data);
  }

  bool wait_for(T &data, int64_t id, std::chrono::milliseconds timeout) {
    return sync_pair_.wait_for(data, id, timeout) && predicate_(data);
  }

  bool wait_until(T &data, int64_t id, std::chrono::time_point<std::chrono::steady_clock> timeout) {
    return sync_pair_.wait_until(data, id, timeout) && predicate_(data);
  }

  bool wait_for(int64_t id, std::chrono::milliseconds timeout) {
    T data;
    return wait_for(data, id, timeout);
  }

  bool wait_until(int64_t id, std::chrono::steady_clock::time_point deadline) {
    T data;
    return wait_until(data, id, deadline);
  }

  bool wait_until(int64_t id, std::chrono::milliseconds timeout) {
    return wait_for(id, timeout);
  }

  bool wait(int64_t id) {
    T data;
    return wait(data, id);
  }

  int64_t fetch_id() {
    return sync_pair_.fetch_id();
  }

  void notify(int64_t id, T data) {
    sync_pair_.notify(id, data);
  }

  private:
  SyncPair<T> sync_pair_;
  std::function<bool(T &)> predicate_;
  std::atomic_int64_t seqId_{};
};

template <typename T>
using atrs_t = AsyncResultSyncer<T>;

template <typename T>
atrs_t<T> make_atrs(std::function<bool(T &)> predicate, int64_t seqId = 0) {
  return std::move(atrs_t<T>(std::move(predicate), seqId));
}
}  // namespace detail
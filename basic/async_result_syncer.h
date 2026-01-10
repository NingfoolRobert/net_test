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
#include <mutex>
#include <unordered_map>
#include <utility>

namespace detail {

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
    create_sync_data(id);
    auto &sync_data = get_sync_data(id);
    sync_data.set(data);
  }

  bool wait(T &data, int64_t id) {
    create_sync_data(id);
    auto &sync_data = get_sync_data(id);
    if (sync_data.wait()) {
      auto ret = sync_data.get(data);
      remove_sync_data(id);
      return ret;
    }
    return false;
  }

  bool wait_for(T &data, int64_t id, std::chrono::milliseconds timeout) {
    create_sync_data(id);
    auto &sync_data = get_sync_data(id);
    if (sync_data.wait_for(timeout)) {
      auto ret = sync_data.get(data);
      remove_sync_data(id);
      return ret;
    }
    return false;
  }

  bool wait_until(T &data, int64_t id, std::chrono::time_point<std::chrono::steady_clock> timeout) {
    create_sync_data(id);
    auto &sync_data = get_sync_data(id);
    if (sync_data.wait_util(timeout)) {
      auto ret = sync_data.get(data);
      remove_sync_data(id);
      return ret;
    }
    return false;
  }

  void reset() {
    sync_data_map_.clear();
  }

  int64_t fetch_id() {
    return seqId_++;
  }

  private:
  class SyncData {
public:
    SyncData() noexcept : setter_(), getter_(setter_.get_future()) {
    }
    ~SyncData() {
    }

    SyncData(const SyncData &other) = delete;

    SyncData(SyncData &&other) noexcept {
      if (this != &other) {

        setter_ = std::move(other.setter_);
        getter_ = std::move(other.getter_), is_get_.store(other.is_get_.load());
        is_set_.store(other.is_set_.load());
      }
    }
    SyncData &operator=(const SyncData &other) noexcept {
      this->setter_ = std::move(other.setter_);
      this->getter_ = std::move(other.getter_);
      this->is_get_.store(other.is_get_.load());
      this->is_set_.store(other.is_set_.load());
      return *this;
    }
    SyncData &operator=(SyncData &&other) noexcept {
      this->setter_ = std::move(other.setter_);
      this->getter_ = std::move(other.getter_);
      this->is_get_.store(other.is_get_.load());
      this->is_set_.store(other.is_set_.load());
      return *this;
    }

    bool set(T &data) {
      if (getter_.valid() && !is_set_) {
        setter_.set_value(data);
        is_set_ = true;
        return true;
      }
      return false;
    }

    bool get(T &data) {
      if (getter_.valid() && !is_get_) {
        data = getter_.get();
        is_get_ = true;
        return true;
      }
      return false;
    }

    bool wait_for(std::chrono::milliseconds timeout) {
      return getter_.wait_for(timeout) == std::future_status::ready;
    }

    bool wait_unil(std::chrono::time_point<std::chrono::steady_clock> timeout) {
      return getter_.wait_until(timeout) == std::future_status::ready;
    }

    bool wait() {
      return getter_.wait();
    }
    bool is_done() {
      return is_set_ && is_get_;
    }

private:
    std::promise<T> setter_;
    std::future<T> getter_;
    std::atomic_bool is_set_{false};
    std::atomic_bool is_get_{false};
  };

  protected:
  void create_sync_data(int64_t id) {
    std::lock_guard<std::mutex> lock(lck_);
    if (sync_data_map_.find(id) != sync_data_map_.end()) {
      return;
    }
    sync_data_map_[id] = SyncData();
  }

  void remove_sync_data(int64_t id) {
    std::lock_guard<std::mutex> lock(lck_);
    if (sync_data_map_.find(id) == sync_data_map_.end()) {
      return;
    }

    if (sync_data_map_[id].is_done()) {
      sync_data_map_.erase(id);
    }
  }

  SyncData &get_sync_data(int64_t id) {
    std::lock_guard<std::mutex> lock(lck_);
    return sync_data_map_[id];
  }

  void clear() {
    std::unique_lock<std::mutex> _(lck_);
    sync_data_map_.clear();
  }

  private:
  std::mutex lck_;
  std::unordered_map<int64_t, SyncData> sync_data_map_;
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

  bool wait_until(int64_t id, std::chrono::milliseconds timeout) {
    T data;
    return wait_until(data, id, timeout);
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
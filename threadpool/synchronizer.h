/**
 * @file sync_waiter.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2024-12-29
 */
#pragma once
#include <future>
#include <stdint.h>
#include <stdio.h>
#include <unordered_map>

template <typename T>
class Synchronizer {

public:
    Synchronizer() {
        future_ = promise_.get_future();
    }
    ~Synchronizer() = default;

public:
    T wait(size_t timeout = 5000) {
        auto ret = future_.wait_for(std::chrono::microseconds(timeout));
        if (ret == std::future_status::timeout) {
            return {};
        }
        return future_.get();
    }

    bool wait(T &output, size_t timeout = std::numeric_limits<size_t>::max()) {
        auto ret = future_.wait_for(std::chrono::microseconds(timeout));
        if (ret == std::future_status::ready) {
            output = future_.get();
        }

        return ret == std::future_status::ready;
    }

    void notify(T &&v) {
        promise_.set_value(std::forward<T>(v));
    }

    T get() {
        return future_.get();
    }

private:
    std::promise<T> promise_;
    std::future<T> future_;
};

using sync_id_t = int64_t;

template <typename T>
class BatchSynchronizer {
public:
    T wait(sync_id_t id, size_t timeout = 5000) {
        if (!sync_list_.count(id)) {
            sync_list_[id] = std::make_shared<Synchronizer<T>>();
        }
        return sync_list_[id]->wait(timeout);
    }

    void notify(sync_id_t id, T &&v) {
        if (!sync_list_.count(id)) {
            auto syncer = std::make_shared<Synchronizer<T>>();
            sync_list_[id] = syncer;
        }
        sync_list_[id]->notify(std::forward<T>(v));
    }

    T get(sync_id_t id) {
        auto it = sync_list_.find(id);
        if (it == sync_list_.end()) {
            return {};
        }
        return it->second->get();
    }

    //
    int64_t fetch_id() {
        return ++id_;
    }

private:
    std::unordered_map<int64_t, std::shared_ptr<Synchronizer<T>>> sync_list_;
    std::atomic_int64_t id_{0};
};

/**
 * @file executor.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2024-12-22
 */
#pragma once
#include "../core/spinlock.h"
#include "synchronizer.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace detail {
using task_t = std::function<void()>;

class Executor {
public:
    //
    struct task_context_t {
        int64_t id;
        task_t task;
        Executor *exec;
    };
    //
private:
    enum OperateTypeEnum : uint8_t
    {
        ADD,
        RMV,
        CLR,
    };
    struct task_operator_t {
        OperateTypeEnum op;
        task_context_t *task_context;
    };

public:
    Executor();
    ~Executor();

    bool add_task(task_context_t *&context, task_t &&task, bool async = true);
    //
    void rmv_task(task_context_t *context);
    //
    bool loop(int timeout = 1000);
    //
    void stop();

private:
    void process_task();

    void process_add(task_context_t *context);

    void process_rmv(task_context_t *context);
    //
    void process_clr(task_context_t *context);
    //
    inline int64_t fetch_id() {
        return syncer_.fetch_id();
    }

private:
    spinlock lck_;
    std::vector<task_context_t *> tasks_;
    std::vector<task_operator_t> task_ops_;
    size_t cap_;
    size_t task_size_;
    bool running_{true};

private:
    BatchSynchronizer<bool> syncer_;
    int64_t     stop_id_;
};

using executor_context_t  = Executor::task_context_t;
using executor_t = Executor;
}  // namespace detail

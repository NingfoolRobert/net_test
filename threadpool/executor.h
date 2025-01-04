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

using task_t = std::function<void()>;

class executor;

//
struct task_context_t {
    int64_t id;
    task_t task;
    executor *exec;
};
//
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

class executor {
public:
    executor();
    ~executor();

    bool add_task(task_context_t *&context, task_t &&task);
    //
    void rmv_task(task_context_t *context);
    //
    bool loop(int timeout);
    //
private:
    void process_task();

    void process_add(task_context_t *context);

    void process_rmv(task_context_t *context);

    void process_clr();
    static int64_t fetch_id() {
        return ++task_id_;
    }

public:
    static std::atomic_int64_t  task_id_;
private:
    spinlock lck_;
    std::vector<task_context_t *> tasks_;
    std::vector<task_operator_t> task_ops_;
    size_t cap_;
    size_t task_size_;

private:
    BatchSynchronizer<bool> syncer_;
};

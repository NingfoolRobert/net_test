#include "executor.h"
#include <mutex>
#include <unistd.h>

namespace detail {
std::atomic_int64_t executor::task_id_ = 0;
executor::executor() : cap_(256), task_size_(0) {
    tasks_.resize(cap_);
}
executor::~executor() {
    process_clr();
}

bool executor::loop(int timeout) {
    while (running_) {
        process_task();
        if (timeout) {
            usleep(timeout);
        }
    }
    return true;
}

bool executor::add_task(task_context_t *&cookie, task_t &&task) {

    auto task_context = new task_context_t();
    task_context->exec = this;
    task_context->task = std::move(task);
    task_context->id = fetch_id();
    cookie = task_context;
    {
        std::unique_lock<spinlock> _(lck_);
        task_ops_.push_back({ADD, task_context});
    }
    return true;
}

void executor::rmv_task(task_context_t *context) {
    if (nullptr == context) {
        return;
    }

    {
        std::unique_lock<spinlock> _(lck_);
        task_ops_.push_back({RMV, context});
    }
    //
    syncer_.wait(context->id);
}

void executor::process_task() {

    std::vector<task_operator_t> task_ops;
    {
        std::unique_lock<spinlock> _(lck_);
        task_ops.swap(task_ops_);
    }

    for (auto i = 0u; i < task_ops.size(); ++i) {
        switch (task_ops[i].op) {
            case OperateTypeEnum::ADD:
                process_add(task_ops[i].task_context);
                break;
            case OperateTypeEnum::RMV:
                process_rmv(task_ops[i].task_context);
                break;
            case OperateTypeEnum::CLR:
                process_clr();
                break;
            default:
                break;
        }
    }
    //
    for (auto i = 0u; i < task_size_; ++i) {
        auto &task = tasks_[i];
        task->task();
    }
}

void executor::process_add(task_context_t *context) {
    std::unique_lock<spinlock> _(lck_);
    if (task_size_ < cap_) {
        tasks_[task_size_] = context;
    }
    else {
        tasks_.push_back(context);
    }
    task_size_++;
}

void executor::process_rmv(task_context_t *context) {
    std::unique_lock<spinlock> _(lck_);
    if (nullptr == context) {
        return;
    }
    //
    for (auto i = 0u; i < task_size_; ++i) {
        if (tasks_[i]->id == context->id) {
            std::swap(tasks_[i], tasks_[task_size_ - 1]);
            delete tasks_[task_size_ - 1];
            tasks_[task_size_ - 1] = nullptr;
            task_size_--;
            syncer_.notify(context->id, true);
            break;
        }
    }
}
//
void executor::process_clr() {
    std::unique_lock<spinlock> _(lck_);
    for (auto i = 0u; i < task_size_; ++i) {
        auto id = tasks_[i]->id;
        delete tasks_[i];
        tasks_[i] = nullptr;
        syncer_.notify(id, true);
    }
}
//
void executor::stop() {
    running_ = false;
}
}  // namespace detail

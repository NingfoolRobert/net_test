#include "executor.h"
#include <mutex>
#include <unistd.h>

namespace detail {
std::atomic_int64_t Executor::task_id_ = 0;
//
Executor::Executor() : cap_(256), task_size_(0) {
    tasks_.resize(cap_);
}
Executor::~Executor() {
    process_clr();
}

bool Executor::loop(int timeout) {
    while (running_) {
        process_task();
        if (timeout > 0) {
            usleep(timeout);
        }
    }
    return true;
}

bool Executor::add_task(task_context_t *&cookie, task_t &&task, bool async /* = true*/) {

    auto task_context = new task_context_t();
    task_context->exec = this;
    task_context->task = std::move(task);
    task_context->id = syncer_.fetch_id();
    cookie = task_context;
    {
        std::unique_lock<spinlock> _(lck_);
        task_ops_.push_back({ADD, task_context});
    }

    // blocking mode
    if (!async && !syncer_.wait(task_context->id)) {
        printf("wait task execute fail. id:%ld", task_context->id);
        return false;
    }

    return true;
}

void Executor::rmv_task(task_context_t *context) {
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

void Executor::process_task() {

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
                process_clr(task_ops[i].task_context);
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
    //
}

void Executor::process_add(task_context_t *context) {
    std::unique_lock<spinlock> _(lck_);
    if (task_size_ < cap_) {
        tasks_[task_size_] = context;
    }
    else {
        tasks_.push_back(context);
    }
    task_size_++;
    syncer_.notify(context->id, true);
}

void Executor::process_rmv(task_context_t *context) {
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
void Executor::process_clr(task_context_t *context) {
    std::unique_lock<spinlock> _(lck_);
    for (auto i = 0u; i < task_size_; ++i) {
        auto id = tasks_[i]->id;
        delete tasks_[i];
        tasks_[i] = nullptr;
        syncer_.notify(id, true);
    }

    syncer_.notify(context->id, true);
}
//
void Executor::stop() {
    // blocking  stop
    auto task_context = new task_context_t();
    task_context->id = syncer_.fetch_id();
    task_context->exec = this;
    {
        std::unique_lock<spinlock> _(lck_);
        task_ops_.push_back({CLR, task_context});
    }
    running_ = false;
    syncer_.wait(task_context->id);
}
}  // namespace detail

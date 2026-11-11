# PollingThreadExecutor 第一版

C++17/Linux x86 执行器，实现依据 [设计文档](../../threadpool/polling_thread_design.md)。按后续简化决定，已移除处理器列表和线程信息查询路径；增删完成通知仍使用 `SyncPair`。本目录独立构建，原有线程草稿、同步器及构建文件保持不变。

## 构建与测试

```sh
cmake -S basic/execution -B /tmp/net-test-execution-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/net-test-execution-build -j 4
ctest --test-dir /tmp/net-test-execution-build --output-on-failure
```

库目标为 `polling_thread_executor`，公开头文件为 `polling_thread_executor.h`，依赖系统线程库。测试依赖已有的 GoogleTest，使用 GNU ld `--wrap` 注入内存分配、线程亲和性失败，并检查休眠路径。仅构建库可传 `-DBUILD_TESTING=OFF`。其他 CMake 项目可直接 `add_subdirectory` 本目录，然后链接该库目标。

`PollingThreadExecutor` 直接持有线程、命令队列、Handler 数组和同步状态；成员声明在头文件中，函数实现在 `.cpp` 中。

```cpp
#include "basic/execution/polling_thread_executor.h"

#include <atomic>
#include <cstdio>

int main() {
  basic::ThreadExecutorConfig config;
  config.name = "event-poller";
  std::atomic<unsigned> calls{0};  // 先构造，保证比 executor 更晚销毁。
  basic::PollingThreadExecutor executor(config);
  if (!executor.Start()) {
    if (auto error = executor.GetError(); !error.empty()) {
      std::fprintf(stderr, "%s\n", error.c_str());
    }
    return 1;
  }

  basic::HandlerCookie* cookie = nullptr;
  if (!executor.AddHandler(cookie, [&calls] { ++calls; }, "counter")) {
    return 2;
  }
  if (!executor.RemoveHandler(cookie)) {
    return 3;
  }
  executor.Stop();
}
```

## 增删与所有权

- 每个实例独占一个工作线程和固定 512 个 `HandlerEntry`，运行时不改变处理器 vector 的大小或容量。
- 默认 `async=false`。仅同步增删判断是否为目标执行器自己的工作线程；异步调用始终入队。
- 本线程同步添加立即注册，新增回调从下一轮执行。本线程同步删除自己或其他 Handler 立即标记注销，尚未执行的目标本轮跳过，当前调用继续，轮尾才移动条目并释放捕获资源和名额。
- 本线程同步删除尚未生效的异步添加立即成功，添加命令处理时检查取消标志，跳过注册并回收资源。名额在该命令被清理后释放。
- 外部同步增删等待执行完成；同步删除成功返回后，回调及捕获资源已经清理。异步返回成功仅表示命令被接纳，不提供成功完成通知。
- 本线程直接操作可以先于已入队命令生效；队列内部保持接纳顺序。
- 添加成功直接覆盖 Cookie 输出，失败保留原值。覆盖不会删除原 Handler。
- Cookie 由执行器内部持有，调用方不得释放或拥有它；不得重复或并发删除同一 Cookie，不得在删除、停止后使用任何指针副本。停止与 Cookie 的获取、使用须由调用方协调。
- 借用资源需要明确的释放时点时，使用外部同步删除，或者协调至 `Stop()` 返回后再释放。满载时本线程标记删除后立即添加仍可能失败。
- 捕获对象析构须不抛异常、不阻塞，且不得在析构期间重入同一执行器的增删接口。

所有实例共享进程级 64 位 HandlerId 分配器，ID 不复用，耗尽后拒绝添加。首版不提供按 HandlerId 查询或删除的接口。

## 生命周期与错误

- 构造只保存、校验配置；无效配置抛 `std::invalid_argument`。配置按值保存，随后固定。
- `bool Start()` 完成线程配置握手后返回。运行中重复启动成功；启动失败返回 `false`，线程收尾后允许重试。请求停止后不再允许启动。
- 外部通过 `void Stop()` 关闭接纳入口并等待命令收尾、资源清理和线程退出，可重复或并发调用；本线程调用抛 `std::logic_error`。不得在本线程析构执行器。
- Handler 抛任何异常时保存首次运行错误的文本，停止后续 Handler，处理所有已接纳命令并通知同步调用方，然后清理线程。标准异常保存 `what()`，非标准异常保存固定说明；异常消息为空或诊断文本分配失败时保留简短的错误说明。
- `std::string GetError() const` 返回错误文本的独立副本，空字符串表示无错误；不再提供 `ExecutorError`、错误码、操作字段、HandlerId 或原始异常指针。启动失败记录错误，重试成功后清空；停止后的启动拒绝保留已有错误，没有已有错误时记录停止说明。错误信息不加锁，由调用方保证 `Start()`、工作线程异常处理和 `GetError()` 之间没有并发的错误信息读写。外部读取运行错误应在 `Stop()` 返回后进行，且不能同时调用 `Start()`；不要通过轮询 `GetError()` 等待异常。

正常业务回调应短小、非阻塞。跨执行器同步调用须避免形成循环等待；执行器只处理自身工作线程的自等待问题。

## 第一版容量与平台规则

管理命令容量为 **1024**，包含队列、剩余批次和执行中的入队命令；每轮最多处理 **64** 条，再执行业务轮询。满载增删返回 `false`。剩余批次先于后来的命令，停止时按原顺序完成全部命令。这两个内部常量已通过满载和轮次公平性测试，尚未针对业务负载调优，不承诺固定时延。

`kBusyPolling` 每轮执行一次 `_mm_pause()`；`kIdle` 每轮使用 `sleep_for`，第一版允许 `(0, 24 小时]`，其他模式忽略该休眠字段。管理操作和停止请求不提前唤醒已进入的休眠。

线程名最多 15 字节，不允许嵌入 NUL。`cpu=-1` 保留继承亲和性，非负值按单个逻辑 CPU 绑定并回读；不支持、不可用或受环境限制的 CPU 在启动时报告错误。普通调度忽略实时优先级，默认 nice 不主动设置；实时调度校验五档实时等级并忽略 nice。配置失败不会静默降级，实时优先级或负 nice 需要相应操作系统权限。

本地 `sync_pair.h` 在 `basic` 中提供独立完成记录实现，保留准备、通知、等待、取消和安全回收语义。原 `basic/async_result_syncer.h` 不修改，也不作为新模块的依赖。同步状态随每条同步命令独立创建，正常完成后回收，异步命令不创建同步记录。

## Timers

`basic::TimeWheel` supports one-shot and repeating timers, cancellation, and
restart. Slot counts and tick duration must be positive. Repeat intervals must
be positive. Delays round up to ticks; timing resolution is the base tick.

Callbacks execute serially on the worker, outside the wheel mutex. They may add
or cancel timers, or request `stop()`. A slow callback delays later callbacks;
callback exceptions are contained. External `stop()` joins the worker and clears
pending timers, including when the wheel has not started. A callback's `stop()`
requests worker termination; an external stop/destructor completes cleanup.
Destroy the wheel outside its callbacks. Cancellation prevents pending callbacks;
it does not interrupt a callback already executing.


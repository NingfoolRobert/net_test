# Folly 与 Google 线程方案调研

核对日期：2026-10-18。范围：本地 Folly、Folly 官方 `main`、Google Abseil 和 gRPC 官方 `master`；下述建议针对 C++17/Linux 固定线程持续 polling。没有运行这些库的性能对比，不把架构判断表述为性能实测。

## 结论与选型建议

**建议保留独立 PollingThread：参考 Folly EventBase 的单线程所有权和跨线程命令机制，参考 Google 库对同步、取消和生命周期的明确约束。** 目前查看的公开接口均未直接提供本需求的“持续轮询任务注册表 + 同步删除并等待在途回调 + 任务属性查询”。这是基于以下接口核对的判断，不代表这些项目不存在其他线程组件。

| 组件 | 已核实的执行模型 | 对本需求的判断 |
|---|---|---|
| Folly CPUThreadPoolExecutor | 队列中取出回调执行，任务队列可分优先级 | 适合一次性计算任务；持续 polling 需额外注册表 |
| Folly IOThreadPoolExecutor / EventBase | 每线程 EventBase，可向指定 EventBase 投递 | EventBase 最接近；已依赖 Folly 的应用值得复用 |
| Abseil synchronization | Mutex、Notification 等同步原语 | 可借鉴同步契约，不能直接作为 polling 执行器 |
| gRPC EventEngine / WorkStealingThreadPool | 异步回调、定时任务、多队列工作窃取与弹性线程 | 适合 RPC 运行时；固定核心业务线程不宜直接套用 |

各行事实的官方依据见下文；复用价值为本项目的设计判断。

## Folly：线程、任务队列与事件循环分层

**事实。** CPUThreadPoolExecutor 的优先级由多个任务队列实现，工作线程先检查高优先级队列；它不因此设置 OS 线程优先级，也不抢占正在运行的低优先级回调。当前官方实现默认选择无界队列，并可选择 LifoSem / ThrottledLifoSem；本地旧版头文件描述的是默认有界队列，不能混用版本结论。[CPUThreadPoolExecutor.h](https://github.com/facebook/folly/blob/main/folly/executors/CPUThreadPoolExecutor.h)、[CPUThreadPoolExecutor.cpp](https://github.com/facebook/folly/blob/main/folly/executors/CPUThreadPoolExecutor.cpp)

**事实。** IOThreadPoolExecutor 在工作线程中运行 EventBase，并提供 `getEventBase()` / `getAllEventBases()`；向选定的 EventBase 投递可指定执行线程。普通 `add()` 经线程选择后投递，不能据此推导业务任务永久绑定某个核心。[IOThreadPoolExecutor.h](https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.h)、[IOThreadPoolExecutor.cpp](https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.cpp)

**事实。** EventBase 的 `runInLoop()` 调度一次回调，重复 polling 需要回调自行重新注册；`runInLoop()` 限本线程，跨线程使用 `runInEventBaseThread()`。`cancelLoopCallback()` 只是取消挂起注册，不是跨线程等待屏障。`loopPoll()` 允许外部逐轮驱动，需要配对 setup / cleanup；`loopForever()` 持续运行并不等于持续忙轮询。[EventBase.h](https://github.com/facebook/folly/blob/main/folly/io/async/EventBase.h)

**推断。** 如选择 EventBase 实现本需求，应让任务对象及注册表归 EventBase 线程所有；管理线程投递删除命令，待当前回调返回后取消下一轮注册并销毁任务，再返回完成通知。还需处理自等待、停止期间命令和回调生命周期，不能仅调用 `cancelLoopCallback()` 就释放跨线程借用资源。

**事实。** `ThreadFactory` 负责创建线程，`InitThreadFactory` 在目标线程执行初始化再进入工作函数。`PriorityThreadFactory` 设置的是 nice，底层调用 `setpriority(PRIO_PROCESS, 0, priority)`；失败记录警告后继续。它不设置 `SCHED_FIFO` / `SCHED_RR` 的静态实时优先级。[ThreadFactory.h](https://github.com/facebook/folly/blob/main/folly/executors/thread_factory/ThreadFactory.h)、[InitThreadFactory.h](https://github.com/facebook/folly/blob/main/folly/executors/thread_factory/InitThreadFactory.h)、[PriorityThreadFactory.h](https://github.com/facebook/folly/blob/main/folly/executors/thread_factory/PriorityThreadFactory.h)、[PriorityThreadFactory.cpp](https://github.com/facebook/folly/blob/main/folly/executors/thread_factory/PriorityThreadFactory.cpp)

**建议。** 借鉴启动初始化钩子，但本模块应在 affinity / priority 设置完成并回读后才允许执行任务；设置失败返回错误。配置分别表示任务排序、普通线程 nice、实时调度 policy / priority，不复用一个 `priority` 字段。

## Google：Abseil 同步原语与 gRPC 执行运行时

**事实。** Abseil 公开 synchronization 指南涵盖 Mutex、Notification、Barrier、BlockingCounter 等同步原语。[Synchronization Library](https://abseil.io/docs/cpp/guides/synchronization)

**事实。** `absl/synchronization/internal/thread_pool.h` 确实有线程池，但源码明确用于测试：共享 FIFO 队列、Mutex 条件等待、线程执行回调、析构投递结束信号并 join。Abseil 兼容性规则明确排除 `internal` / `test` 等实现细节，不能将这个类视为受支持的生产线程池 API。[测试辅助线程池](https://github.com/abseil/abseil-cpp/blob/master/absl/synchronization/internal/thread_pool.h)、[兼容性规则](https://abseil.io/about/compatibility)

**事实。** gRPC EventEngine 的 `Run()` 是不可取消的一次性异步执行；`RunAfter()` 返回 TaskHandle。`Cancel(handle)` 成功才保证回调不会执行；false 本身没有等待在途回调结束的保证。接口位于 experimental 命名空间。[EventEngine 契约](https://github.com/grpc/grpc/blob/master/include/grpc/event_engine/event_engine.h)

**事实。** WorkStealingThreadPool 在线程内部提交时优先使用该线程的局部队列，外部提交进入全局队列；工作线程依次尝试局部队列、全局队列、偷取其他队列，然后进行带退避的条件等待。Lifeguard 根据负载启动线程；`reserve_threads` 不能理解为固定线程上限。公开接口没有 CPU 绑定、OS priority、微秒 idle 策略、周期任务属性表或按 ID 删除 API。[WorkStealingThreadPool 实现](https://github.com/grpc/grpc/blob/master/src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.cc)、[公开声明](https://github.com/grpc/grpc/blob/master/src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h)

**事实。** gRPC `ThreadPool::Quiesce()` 是整个池的关闭操作，不是删除单个任务；接口允许在线程池线程调用，具体实现对这种情况只等待其他工作线程退出。其实现依赖 gRPC 内部队列、同步、时间、线程计数及 Abseil，不能当作一个独立头文件引入。[ThreadPool 接口](https://github.com/grpc/grpc/blob/master/src/core/lib/event_engine/thread_pool/thread_pool.h)、[实现依赖](https://github.com/grpc/grpc/blob/master/src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h)

**推断。** 工作窃取允许任务换线程，不满足任务固定在一个核心的要求；把每个工作线程都设 affinity 也不等于固定任务归属。用永久不返回的 polling 回调占住 RPC 池线程，会使运行时看到持续繁忙，增加调度和关闭复杂度。这里适合借鉴命令唤醒、显式关闭与所有权约束，不建议直接复用内部线程池。

## 本项目应保留的契约

以下为建议，不是第三方库已有保证：

1. 一个实例拥有一个线程与任务注册表，任务在注册后每轮执行一次；任务不能阻塞或无限执行。多核用多个实例，显式分配归属。
2. 添加、删除和属性查询在工作线程轮次边界执行；同步删除返回时保证旧回调已经结束且不再进入下一次调用。回调内同步删除需要拒绝自等待或另设异步接口。
3. 属性查询返回任务 ID、名称、用户属性、启用状态和可选计数的快照；第三方线程池的队列长度和线程统计不能替代业务属性表。
4. 线程配置分别提供 CPU 集合、调度策略、实时 priority 和可选 nice，并显示实际生效值。单个 CPU 是绑核，CPU 集合是允许运行范围；cpuset 还可能进一步限制它。[Linux affinity 约束](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html)
5. `BusySpin`、`Yield`、`Sleep(us)` 独立选择，并说明仅空闲轮次还是每轮让渡；sleep 应可被管理操作和停止唤醒。微秒是配置单位，不能承诺精确微秒唤醒；sleep 到期后还可能等待调度，yield 在普通调度策略下也不提供固定延迟。[nanosleep](https://man7.org/linux/man-pages/man2/nanosleep.2.html)、[sched_yield](https://man7.org/linux/man-pages/man2/sched_yield.2.html)

## 版本与依赖限制

本地 Folly 提交为 `7828768079d6876db2834bd75d7356757f88be86`，日期 2025-01-04；本地 README 和 CMake 默认 C++17。官方当前 README 已写明 C++20，且不保证跨提交 ABI 兼容，构建还需解析其依赖。因而不能为了参考线程设计就把本地库升级至 main；若选择直接复用，应先锁定兼容版本并单独验证构建。[Folly README](https://github.com/facebook/folly/blob/main/README.md)

本报告仅完成源码和文档核对；没有构建 Folly / gRPC、没有测量延迟、也未验证目标机器的实时调度权限。最终是否值得引入 Folly，需用业务负载比较持续 polling 的吞吐、尾延迟、控制命令延迟与空闲 CPU 成本。

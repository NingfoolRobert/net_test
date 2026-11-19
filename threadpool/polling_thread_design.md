# PollingThreadExecutor 设计文档

本文记录讨论确定的设计，目标环境为 C++17/Linux。本文不表示实现已经完成或通过性能验证。现有 polling_thread.h、polling_thread.cpp 是前期未验证草稿，不代表本文确定的接口和存储结构。

## 1. 定位与职责

PollingThreadExecutor 是面向低时延、高性能场景的单工作线程轮询执行器。一个实例拥有一个工作线程，持续调用已注册事件处理器；多核部署通过多个实例完成。

执行器负责：

1. 线程命名、单 CPU 绑核、调度策略与线程优先级、CPU 让渡与微秒休眠配置。
2. 工作线程的启动、停止和资源清理。
3. 事件处理器的同步/异步添加、同步/异步删除和存储管理。
4. 分配进程内全局唯一的 HandlerId，通过 Cookie 快速定位处理器。
5. 查询当前 polling 处理器和线程配置。

事件处理器采用短小、非阻塞、耗时基本确定的回调；每轮依次调用所有已注册处理器，由处理器检查并处理自己的事件，无事件时快速返回。回调不返回调度状态。执行器不保存业务执行进度，不引入处理器状态机。

本阶段不负责跨线程提交业务任务并回传结果、协程挂起恢复、业务事件等待、定时业务任务、工作窃取、任务迁移、任务级优先级或抢占式调度。不强制终止正在执行的回调。首版不提供按 HandlerId 查询或删除的接口，不增加 HandlerId 哈希索引；HandlerId 用于身份标识、快照和错误诊断，删除继续使用有效 Cookie。

“异步”在当前范围内指管理命令接纳后立即返回，调用方按成功处理，不提供单独的成功完成通知，但返回不代表命令已执行完成。“同步”默认由外部调用方等待工作线程处理完成；仅同步增删检查调用者是否为本执行器的工作线程，本线程同步添加直接注册，同步删除直接标记注销、轮尾回收。异步增删不进行本线程调用检查，统一入队，由工作线程在轮次边界处理。外部线程不能直接修改处理器数组。管理操作的完成确认不属于业务结果回传。

## 2. 已确定的核心约束

| 项目 | 设计 |
|---|---|
| 类名 | PollingThreadExecutor |
| 配置传入 | 构造函数接收 ThreadExecutorConfig，执行器独立保存配置，Start 使用该配置启动线程 |
| 处理器类型 | using Handler = std::function<void()>; |
| 存储 | std::vector<HandlerEntry>，初始化为 512 个元素 |
| 有效区域 | [0, handler_count_) 连续存放；本线程同步删除的条目保留至轮尾清理，以 pending_remove 标记跳过 |
| 容量变化 | 运行期间不扩容、不缩容、不调用 pop_back() |
| polling | 固定轮初遍历数量，跳过 pending_remove 条目，直接调用 handlers_[i].callback() |
| 轮次结束 | kBusyPolling 执行 _mm_pause()；kIdle 执行 sleep_for(sleep_us)，按模式二选一 |
| 删除 | 尾部有效 HandlerEntry 移动覆盖删除位置，然后清空尾部 |
| 顺序 | 不保证删除后保持注册顺序 |
| HandlerId | 所有实例间进程内全局唯一，不改变、不复用 |
| 添加结果 | 返回 bool；成功时设置 HandlerCookie* 输出参数，失败时保持输出参数原值 |
| 增删模式 | bool async = false；同步时检查本线程并选择直接处理或入队等待；异步始终入队 |
| 同步通知 | 外部同步增删复用 detail::SyncPair<bool>，每条入队同步命令使用独立命令 ID；本线程直接路径不使用 SyncPair |
| 管理命令锁 | 复用 basic/concurrency/spin_lock.h 中的 SpinLock，保护命令接纳、名额预留和缓存交换 |
| Cookie | 独立对象，内部通过 shared_ptr 持有，调用方持有非拥有型 HandlerCookie*；移动条目时地址不变 |
| 运行异常 | 保存首次运行异常及 HandlerId，关闭接纳入口、停止轮询并完成命令和资源收尾 |
| 管理队列 | 有界，满载拒绝；每轮有处理数量预算，未处理完的批次保留原顺序 |
| 查询 | 外部线程同步入队，本线程直接生成独立快照，排除已标记注销条目 |

不采用紧凑槽位号数组或 HandlerEntry 指针数组进行 polling，也不通过 Cookie 间接定位回调。Cookie 只用于管理操作。

## 3. 线程配置

线程配置通过 ThreadExecutorConfig 在执行器构造时传入。执行器按值保存独立配置，不持有调用方配置对象的引用或指针；调用方后续修改原配置不影响执行器。构造完成后配置固定，本阶段不支持运行时修改。

配置结构及默认值如下，SchedulingPolicy 和 IdleStrategy 分别表示下表中的调度策略及轮尾处理方式：

~~~cpp
enum class IdleStrategy { kBusyPolling, kIdle };

enum class RealtimePriority : int {
  kLowest = 1,
  kLower = 30,
  kMedium = 50,
  kHigher = 70,
  kHighest = 99
};

enum class NicePriority { kLowest, kLower, kMedium, kHigher, kHighest };

struct ThreadExecutorConfig {
  std::string executor_name;
  int cpu = -1;
  SchedulingPolicy scheduling_policy = SchedulingPolicy::kOther;  // 普通调度。
  RealtimePriority priority = RealtimePriority::kMedium;  // 仅实时调度生效。
  NicePriority nice_value = NicePriority::kMedium;  // 仅普通调度生效。
  IdleStrategy idle_strategy = IdleStrategy::kBusyPolling;
  std::chrono::microseconds sleep_us{1};
};

struct StartError {
  std::error_code code;
  std::string operation;
  std::string message;
};

// HandlerId 为 std::uint64_t，0 表示没有对应的 Handler。
struct RuntimeError {
  HandlerId handler_id = 0;
  std::exception_ptr exception;
};

// PollingThreadExecutor 的构造函数声明。
explicit PollingThreadExecutor(ThreadExecutorConfig config);

// 创建工作线程并握手确认配置生效，启动成功返回 true，失败返回 false。
bool Start();

// 返回最近一次 Start 的错误快照；无启动错误时为空。
std::optional<StartError> GetLastStartError() const;

// 保存首次导致停止的运行异常，停止后仍可查询。
std::optional<RuntimeError> GetLastRuntimeError() const;

// 关闭管理接纳入口并请求停止，不等待线程退出，允许本线程调用。
void RequestStop() noexcept;

// 请求停止并等待工作线程退出；幂等，本工作线程调用时抛逻辑错误。
void Stop();
~~~

构造时校验配置的取值和字段组合，并在需要时完成规范化，再保存配置。构造不启动工作线程；Start 返回 bool，使用已保存的配置创建线程，在工作线程内设置名称、亲和性和调度参数，并握手确认生效后才返回 true、允许执行回调。线程创建、操作系统配置或生效校验失败时，完成本次启动线程的收尾后返回 false，不执行回调。构造阶段的配置校验不属于 Start 的返回值处理范围。

启动失败原因通过 GetLastStartError() 获取，StartError 包含系统或执行器错误码 code、失败步骤 operation（例如 pthread_setaffinity_np）和描述 message。初始无错误时返回 std::nullopt；Start 返回 false 前保存对应错误，启动成功时清空错误。查询返回独立副本，错误记录的更新与读取由内部同步保护，不能暴露内部可变引用。该快照表示最近一次 Start 的结果，多次启动调用可能覆盖诊断，调用方需要将错误关联到某次调用时，应协调 Start 与随后查询的顺序。

重复启动与重试规则：

- 执行器正在运行时，重复 Start 返回 true，不创建新线程、不重复设置线程配置。
- 上次启动失败且尚未请求停止时，允许再次 Start，使用已保存的同一份配置创建新线程、重新应用全部设置并握手确认；上次失败线程必须已经回收。
- RequestStop 或 Stop 一旦请求停止，就不再允许启动；停止中或已停止时 Start 返回 false，并保存不允许启动的诊断。停止后需要运行时创建新的执行器实例。
- Start 与 Stop 的线程创建、回收使用同一生命周期同步协议串行处理；RequestStop 关闭接纳入口与启动开放入口使用同一管理锁，启动不能重新开放已经请求停止的入口。

~~~cpp
ThreadExecutorConfig config;
config.executor_name = "event-poller";
config.cpu = -1;  // 不设置绑核；绑定时填写目标逻辑 CPU 编号。
config.idle_strategy = IdleStrategy::kIdle;
config.sleep_us = std::chrono::microseconds{10};

PollingThreadExecutor executor(std::move(config));
// 随后调用 Start 启动工作线程，启动接口不再接收配置。
~~~

| 配置 | 含义 |
|---|---|
| executor_name | 执行器名称，同时用于设置工作线程的操作系统名称；空字符串表示不额外设置线程名，非空时校验平台长度限制 |
| cpu | 默认 -1，表示不设置绑核、保留线程继承的亲和性；>= 0 表示绑定到该逻辑 CPU 编号，0 为有效编号 |
| scheduling_policy | 默认 SCHED_OTHER，表示普通调度；可显式选择 SCHED_FIFO 或 SCHED_RR |
| priority | 实时优先级等级，默认 RealtimePriority::kMedium（50），仅 kFifo / kRoundRobin 生效；kOther 忽略该字段且不校验其等级，实际系统静态优先级固定为 0 |
| nice_value | 普通调度的五档 nice 等级，默认 NicePriority::kMedium，表示不主动设置、保留继承值；仅 kOther 生效，实时策略忽略该字段 |
| idle_strategy | 默认 kBusyPolling，每轮结束执行 _mm_pause()；kIdle 每轮结束执行 sleep_for(sleep_us) |
| sleep_us | kIdle 模式下的线程休眠时长，单位微秒，必须为有效正值；kBusyPolling 模式不使用该字段 |

CPU 配置使用单个有符号整数，cpu < -1 为无效配置，在构造时拒绝。cpu >= 0 时，启动阶段将亲和性设置为仅包含指定 CPU 的集合；该值为逻辑 CPU 编号，不是可用 CPU 列表中的位置。若编号不受支持、CPU 不可用或受环境限制导致绑定失败，启动明确报告错误，不静默降级为不绑核。配置完成后才允许调用处理器。

默认仍使用普通调度 scheduling_policy=kOther，传给系统的静态优先级为 0。priority 字段专门保存实时等级，默认 kMedium；kOther 下不读取该字段来决定调度行为，也不执行实时等级校验。只有调用方显式选择 kFifo 或 kRoundRobin 时，才校验该等级并转换为系统静态优先级。

实时绑核模式指 cpu >= 0，且 scheduling_policy 为 kFifo 或 kRoundRobin。该模式的优先级分为五档，数值沿用项目现有 [CPUPriorityEnum](bind_processor_helper.h) 的映射：

| 实时优先级等级 | priority 枚举值 | 系统静态优先级 |
|---|---|---|
| 最低 | kLowest | 1 |
| 较低 | kLower | 30 |
| 中等（默认） | kMedium | 50 |
| 较高 | kHigher | 70 |
| 最高 | kHighest | 99 |

实时绑核模式只接受这五个 priority 枚举值，其他值在构造时拒绝；转换后的数值仍须满足对应系统调度策略的有效范围。实时中等优先级 50 与普通调度系统静态优先级 0 含义不同。切换为实时绑核策略时，不指定 priority 就使用 kMedium，也可显式选择其他等级。

~~~cpp
ThreadExecutorConfig config;
config.cpu = 2;  // 示例：目标逻辑 CPU 编号必须在部署环境中可用。
config.scheduling_policy = SchedulingPolicy::kFifo;
// priority 默认 kMedium，对应实时静态优先级 50。
// 需要其他等级时，例如：config.priority = RealtimePriority::kHigher;
~~~

普通调度和实时调度统一按以下规则取得传给系统的优先级：

~~~cpp
const int native_priority =
    config.scheduling_policy == SchedulingPolicy::kOther
        ? 0
        : static_cast<int>(config.priority);
~~~

nice_value 也划分为五档，用独立的 NicePriority 枚举表达。普通调度下采用以下映射，kMedium 明确表示不设置，不等于向系统设置 nice=0：

| 普通优先级等级 | nice_value 枚举值 | 设置行为 |
|---|---|---|
| 最低 | kLowest | 设置 nice=19 |
| 较低 | kLower | 设置 nice=10 |
| 中等（默认） | kMedium | 不调用 setpriority，保留继承值 |
| 较高 | kHigher | 设置 nice=-10 |
| 最高 | kHighest | 设置 nice=-20 |

上述四个具体数值为执行器的等级映射约定。NicePriority 的底层枚举编号不代表系统 nice 值，必须按表转换；特别不能把 kMedium 转成数值后传给 setpriority。保留继承值不保证实际 nice 为 0，也不保证实际调度权重处于其他四档中间。

kOther 下校验 nice_value 等级，在工作线程中对非 kMedium 等级调用 setpriority，并回读确认；设置失败明确报告启动错误。kFifo / kRoundRobin 下忽略 nice_value，不执行其等级校验，也不调用 setpriority。

~~~cpp
ThreadExecutorConfig config;
config.scheduling_policy = SchedulingPolicy::kOther;
config.nice_value = NicePriority::kMedium;  // 默认，不设置 nice。
// 需要降低权重时，例如：config.nice_value = NicePriority::kLower;
~~~

亲和性和调度参数应回读，确认实际生效值。pthread 函数直接返回错误码，不能用 errno 替代；nice 等接口按各自系统契约处理。

Handler 没有业务忙闲反馈，因此不支持“按本轮是否处理业务数据自动休眠”。线程休眠影响整个执行器，不表示单个处理器的等待。

每轮处理器调用完成后，根据 idle_strategy 执行一种轮尾动作，包括没有已注册处理器的轮次。kBusyPolling 调用 _mm_pause()，所需头文件为 <immintrin.h>，沿用项目现有的 x86 intrinsic 用法；它提供 CPU 自旋提示，不作为操作系统线程让渡、休眠或内存同步机制，也不保证固定时长。kIdle 调用 std::this_thread::sleep_for(sleep_us)，不执行 pause；该模式每轮固定休眠，不自动检测业务是否空闲。

不提供工作线程提前唤醒机制。kIdle 模式下，管理命令和停止请求均等待本次休眠自然结束后处理。微秒是配置单位，不是精确返回保证。同步增删、停止及析构的等待时间可能包含剩余休眠时间，并受回调耗时、命令积压和系统调度影响。

## 4. 数据结构与所有权

以下代码为结构示意，不是完整实现。Cookie 字段实际应封装为执行器内部可写，调用方持有非拥有型 HandlerCookie*，不读取或修改位置及归属字段。

~~~cpp
using Handler = std::function<void()>;
using HandlerId = std::uint64_t;

struct executor_cookie_t {
  HandlerId handler_id;
  std::size_t handler_index;
  PollingThreadExecutor* owner;
  bool cancel_pending_add = false;
};

using HandlerCookie = executor_cookie_t;

struct HandlerEntry {
  std::shared_ptr<executor_cookie_t> cookie;
  Handler callback;
  std::string name;
  bool pending_remove = false;
};

static constexpr std::size_t kMaxHandlers = 512;
static constexpr std::size_t kInvalidIndex =
    std::numeric_limits<std::size_t>::max();

std::vector<HandlerEntry> handlers_{kMaxHandlers};
std::size_t handler_count_ = 0;
~~~

vector 的 size() 始终为 512。仅 reserve(512) 不满足要求，因为它不创建可访问元素。前 handler_count_ 个条目持有资源，其余条目为空；前缀可暂时包含已注销但尚未回收的 pending_remove 条目，轮尾清理后恢复为全部有效、无空洞的前缀。该标志仅表示删除与回收过程，不保存业务执行进度，也不引入业务状态机。

pending_remove 仅由工作线程读写，使用普通 bool。执行器可另设 has_pending_removals_，没有待回收条目时跳过轮尾清理扫描。

Cookie 的 cancel_pending_add 仅用于取消已接纳但尚未注册的异步添加，创建时为 false，发布后仅由工作线程读写，使用普通 bool。本线程同步删除此类处理器时设置该标志，待处理添加命令继续持有 Cookie、回调和名称；工作线程取到该添加命令后先检查取消标志，跳过注册并清理资源、释放预留名额。该标志不进入正常 polling 路径。

名称仅用于展示，可重复，不参与定位。首版不引入通用属性字典或默认逐次耗时统计。

HandlerCookie 是 executor_cookie_t 的类型别名，用于公共接口；内部 Cookie 类型和 shared_ptr 存储保持不变。

所有权：

- 添加接纳后、尚未注册时，添加命令持有 Cookie、回调和名称；成功返回时，调用方取得同一 Cookie 的非拥有型裸指针。
- 注册后，对应 HandlerEntry 持有 Cookie、回调和名称；标记注销后仍持有这些资源，直到轮尾清理完成。
- 调用方不得自行 delete Cookie，也不得用该裸指针构造拥有型 shared_ptr；复制或置空裸指针不等于注销处理器。
- 移动 HandlerEntry 转移 shared_ptr，不移动 Cookie 对象，不在 polling 路径复制 shared_ptr。
- 删除或停止释放内部 Cookie 引用；最后一个内部 shared_ptr 释放时销毁 Cookie，外部裸指针不延长其生命周期。
- owner 为非拥有型指针，仅用于管理操作的归属校验，不延长执行器生命周期，也不隐式转发到其他执行器。

## 5. HandlerId 和 Cookie

HandlerId 是身份，handler_index 是物理位置，不能混用。

- 所有实例共享一个进程级 64 位原子 ID 分配器。
- 0 为无效值，从 1 开始分配；执行器销毁重建不重置分配器。
- ID 分配后不改变、不回收；失败留下 ID 空洞可以接受。
- ID 耗尽时拒绝添加，禁止溢出回绕。
- 全局分配器只用于添加，不进入 polling 路径。
- 不再将局部槽位号和代次编码进 HandlerId。

管理定位为 Cookie → handler_index → handlers_[handler_index]。由工作线程校验 owner、下标范围以及 handlers_[index].cookie.get() == cookie。调用方必须保证传入的裸指针仍然有效；上述校验不能使悬空指针重新安全。

Cookie 地址在处理器移动期间不变；handler_index 由工作线程更新。内部 shared_ptr 管理 Cookie 生命周期，不自动同步其字段，也不为外部裸指针提供存活保证。

使用约束：

1. 不并发、重复地使用同一个 Cookie 提交删除。
2. 异步删除接纳成功或同步删除处理成功后，RemoveHandler 在返回前将传入的 cookie 变量置为 nullptr；返回 false 时保持原值。删除命令接纳后，调用方不再使用该 Cookie 的其他指针副本；执行器通过待处理添加命令或已注册条目的内部引用，保证 Cookie 存活至删除处理完成。
3. 本线程同步标记注销成功后，不再使用该 Cookie 的任何副本，即使内部资源尚未回收。删除资源清理完成后 Cookie 裸指针失效，不能再查询、删除或解引用；首版不支持过期 Cookie。
4. 停止或销毁执行器后，其全部外部 Cookie 裸指针失效，不得再使用。
5. 调用方负责协调停止、销毁与 Cookie 的使用，不能在停止清理可能释放 Cookie 时继续提交管理操作。
6. 不从外部读取 Cookie 的位置、归属字段判断完成；AddHandler 和 RemoveHandler 都会修改 Cookie 参数变量，其并发读写须由调用方同步。

## 6. 添加接口与同步/异步语义

已确定接口：

~~~cpp
bool AddHandler(HandlerCookie*& cookie, Handler handler,
                std::string name = {}, bool async = false);
~~~

语义：

1. async=true 时，返回 true 表示 Cookie 已创建、名额已预留、添加命令已接纳，并在返回前将 Cookie 裸指针写入 cookie 输出参数；不代表已经开始 polling。
2. 空回调、容量不足、执行器不接收命令、内部资源准备失败时返回 false，保持 cookie 原值，不遗留处理器或占用名额。
3. 成功返回前准备必要资源；工作线程注册时只向已有 HandlerEntry 移动 Cookie、回调和名称。命令接纳与名额预留失败时必须回滚，不提前覆盖输出参数。
4. 注册前 handler_index 为无效值，注册时由工作线程填写；调用方不通过此字段轮询生效情况。
5. 注册写入 handlers_[handler_count_]，填写位置并增加 handler_count_。
6. 参数在进入 AddHandler 之前的构造可能抛异常；不把这类情况宣称为函数返回 false，是否声明 noexcept 需实现时核对。
7. 输出裸指针不拥有 Cookie。调用方须协调停止与 Cookie 的取得、使用；停止与添加并发时，已接纳处理器可能在 AddHandler 返回前被清理，成功不保证输出指针在停止后仍有效。
8. async=false（默认）时，才检查调用者是否为本执行器的工作线程。外部线程入队并等待注册完成后返回 true、设置 cookie；本线程直接注册并设置 cookie，不入队、不等待、不创建同步记录。本线程在 polling 回调内直接添加的处理器从下一轮开始调用。外部同步添加不保证返回前回调未被调用过，也不保证停止并发清理后指针仍有效。
9. async=true 时不检查调用线程身份，本线程与外部线程都走相同的入队接纳路径，不提供单独的成功完成通知。
10. Cookie 创建时即填写 owner，handler_index 初始为无效值。工作线程处理异步添加命令时，先检查 cancel_pending_add；已取消则不写入 handlers_、不增加 handler_count_、不调用回调，清理命令持有的资源并释放预留名额。尚未取消时按正常流程注册。
11. cookie 参数仅作为输出，不检查其原值是否为空，也不读取原值对应的 Cookie；添加成功时直接覆盖为新 Cookie，失败仍保持原值。覆盖输出变量不删除、不替换原 Handler，也不转移或释放原 Cookie 的所有权；调用方如需管理原 Handler，须自行保留其有效 Cookie。

~~~cpp
HandlerCookie* cookie = nullptr;
if (executor.AddHandler(cookie, [&receiver] { receiver.Poll(); },
                        "market_receiver")) {
  // 默认同步添加，处理器已经注册。
}

HandlerCookie* async_cookie = nullptr;
if (executor.AddHandler(async_cookie, [&receiver] { receiver.Poll(); },
                        "market_receiver", true)) {
  // 显式异步添加，命令已接纳，不保证已经开始 polling。
}
~~~

容量检查必须包含已接纳但尚未注册的添加，不能只看 handler_count_。本线程直接添加也必须使用同一管理锁完成停止接纳检查和名额预留，资源准备及注册在锁外执行；直接路径失败时同样回滚预留。尚未处理的删除和已标记但尚未回收的条目仍占名额，资源清理完成后释放。因此满载时本线程同步标记删除后立即添加仍可能返回 false。

添加成功后可立即提交异步删除；同一队列按命令接纳顺序处理，必须先处理添加命令再处理删除命令。本线程同步路径可以先于队列中的早期命令生效，不再承诺直接操作与队列命令之间的全局执行顺序。本线程同步删除尚未生效的异步添加时，标记取消并返回成功，添加命令原位保留、取出时跳过注册，不提前递归处理或调整队列顺序。

## 7. 同步/异步删除与尾部填补

删除接口：

~~~cpp
bool RemoveHandler(HandlerCookie*& cookie, bool async = false);
~~~

async=true 时不检查调用线程身份，统一入队；返回 true 表示删除命令已被接纳，函数在返回前将 cookie 置为 nullptr，调用方按成功处理，不提供单独的成功完成通知，但此时不保证删除已经完成。

async=false（默认）时才检查是否为本执行器的工作线程：

- 外部线程入队等待，工作线程移除处理器、清理回调及捕获资源并释放名额后，调用方才返回 true 并将 cookie 置空；此时可以安全释放该回调借用的外部资源。
- 本线程直接校验已注册条目并设置 pending_remove，注销 Cookie 的 owner 和 handler_index，将调用方 cookie 置空后返回 true，不入队、不等待、不创建同步记录。可删除当前 Handler，也可删除其他 Handler；成功表示注销完成，回调、捕获资源及名额留到轮尾回收，不能据此提前释放当前调用仍在使用的资源。
- 本线程遇到尚未注册的异步添加时，在有效 Cookie 归属正确、handler_index 无效且尚未取消的前提下，设置 cancel_pending_add、清除 owner，并将调用方 cookie 置空后返回 true。待处理添加命令的内部引用保证 Cookie 存活；资源及名额在该命令被取出并清理后释放，不保证在当前轮尾释放。外部同步删除仍入队等待，不能在调用线程读取或修改 Cookie 的取消标志。

两种模式返回 false 时均保持 cookie 原值。空指针、执行器不接收命令、命令入队或完成状态准备失败时返回 false；同步删除校验失败，同样返回 false 且不删除处理器。本线程直接路径也须在同一管理锁下完成接纳检查，不能绕过停止入口；标记和回收由工作线程执行，不在管理锁内销毁资源。

入队删除命令按值保存入队前的 Cookie 裸指针，不捕获调用方 cookie 变量的引用；异步模式接纳成功后置空，外部同步模式处理成功后置空。待处理添加命令或已注册条目持有内部 shared_ptr；在调用方遵守不重复、并发删除同一 Cookie 的前提下，依靠队列顺序及停止收尾协议保证删除处理前 Cookie 不被释放。不得从裸指针创建新的 shared_ptr 控制块。

~~~cpp
if (executor.RemoveHandler(cookie)) {
  // 外部线程同步删除成功，cookie 已置空，回调及捕获资源已清理。
  // 本线程同步删除成功则仅保证注销，资源在轮尾回收。
}

if (executor.RemoveHandler(async_cookie, true)) {
  // 显式异步删除，async_cookie 已置空；没有完成通知，不据此释放借用资源。
}
~~~

本线程标记删除后，当前回调继续执行；尚未执行的目标在本轮被跳过，已执行的目标不会再次调度。轮尾统一清理，期间不移动、覆盖或销毁正在执行的回调。轮询提前停止或回调异常退出时，也必须在当前调用退出后执行清理，再进行命令收尾或线程退出。

工作线程在安全的轮次边界完成物理删除：

1. 入队删除先拒绝空 Cookie，并在指针仍有效的前提下校验归属、下标范围与条目对应关系；轮尾回收已标记条目时直接使用工作线程扫描得到的有效下标，不再通过已注销的 Cookie 下标定位。不对悬空 Cookie 解引用或承诺安全拒绝。已取消、尚未注册的异步添加由添加命令自身清理，不调用条目的物理删除流程。
2. **先从条目复制并暂存被删除处理器的 Cookie shared_ptr**，使后续位置移动、注销和完成通知不依赖原条目的存活。
3. 若不是最后一个有效位置，将尾部 HandlerEntry 移动覆盖该位置。
4. 移动处理器仍有效时，立即将其 cookie->handler_index 更新为删除位置的下标 idx；如果补入条目也已标记删除，则保留其 Cookie 注销状态并继续清理。其 HandlerId 和 Cookie 地址不变。
5. 将原尾部 HandlerEntry 置空并减少 handler_count_；不调用 pop_back()。
6. 将被删除 Cookie 的 handler_index 置为无效值并清除 owner（本线程标记路径已完成注销），清理回调及名称、释放名额；入队的外部同步删除通知一次处理结果，异步删除和本线程标记回收不发送完成通知。再释放暂存 Cookie 引用，最后一个内部引用释放后 Cookie 销毁，外部裸指针失效。

以下为条目移动及 Cookie 下标更新的示意，idx 已通过有效区域和 Cookie 对应关系校验；名额释放与完成通知按上述流程处理：

~~~cpp
auto removed_cookie = handlers_[idx].cookie;
const std::size_t last = handler_count_ - 1;
if (idx != last) {
  handlers_[idx] = std::move(handlers_[last]);
  if (!handlers_[idx].pending_remove) {
    handlers_[idx].cookie->handler_index = idx;
  }
}
handlers_[last] = HandlerEntry{};
--handler_count_;
removed_cookie->handler_index = kInvalidIndex;
removed_cookie->owner = nullptr;
~~~

~~~text
删除前：[A] [B] [C] [空] ...  handler_count_ = 3
删除 B：[A] [C] [空] [空] ...  handler_count_ = 2

C 的 HandlerId、Cookie 地址不变；Cookie 中的位置由 2 更新为 1。
~~~

删除最后一个处理器时无需移动。轮尾清理 pending_remove 条目时，尾部填补后重新检查当前位置，不能无条件递增游标，否则可能漏掉补入的待删除条目。清理完成后有效区域保持紧凑，但不再保证注册顺序。

单个条目的结构删除为 O(1)，不包含轮尾清理扫描、用户捕获对象的析构、名称资源释放和通知开销。不能保证任意捕获对象的析构耗时。

## 8. polling 执行路径

~~~cpp
const std::size_t round_count = handler_count_;
for (std::size_t i = 0; i < round_count; ++i) {
  if (!handlers_[i].pending_remove) {
    handlers_[i].callback();
  }
}
// 在轮尾 pause 或 sleep 之前清理标记条目；异常及停止路径也必须清理。
CleanupPendingRemovals();
switch (config_.idle_strategy) {
  case IdleStrategy::kBusyPolling:
    _mm_pause();
    break;
  case IdleStrategy::kIdle:
    std::this_thread::sleep_for(config_.sleep_us);
    break;
}
~~~

实际循环还包括停止检查、管理命令检查和轮尾模式分支。无管理命令时不获取处理器管理锁，不复制回调、不扩容处理器 vector，也不默认逐次读取系统时钟或记录日志。

polling 不访问 Cookie、哈希表、槽位表或 HandlerEntry 指针数组。“无间接访问”限定于处理器存储和定位层面，std::function 自身的类型擦除调用机制仍存在。

回调、回调捕获对象的析构均应避免阻塞。捕获对象的析构必须不抛异常，且不得在析构期间重入同一执行器的增删接口，包括同步和异步模式；资源清理期间不执行这种嵌套管理操作。一个回调处理过多工作仍会增加其他处理器和管理命令的响应时间。

## 9. 并发与管理命令

工作线程独占处理器数组、有效计数、pending_remove 和已注册 Cookie 的位置字段。外部管理线程不能直接修改数组；本线程同步添加仅追加尾部条目，同步删除仅标记注销，遍历期间不移动或销毁条目。

在增删接口中，仅同步增删执行本线程调用检查，判断必须针对目标执行器：A 的工作线程调用 B 的同步接口，属于 B 的外部同步调用。异步路径先按 async 分支进入统一入队流程，不读取或判断工作线程身份。同步查询也识别本线程并直接生成快照，避免等待自身。本线程的判断、直接处理不出现在没有管理操作或查询的正常 polling 路径。

管理命令缓存采用项目现有的 [SpinLock](../basic/concurrency/spin_lock.h) 和 atomic pending 标志：

- 管理命令接纳需要短暂加锁；异步不等于完全无锁或无等待。
- 工作线程在轮次边界取出命令，锁外处理。
- 入队命令按接纳顺序处理，异步增删与外部同步增删使用同一管理队列；本线程同步增删直接处理，不与队列建立全局执行顺序。
- 外部同步模式使用 detail::SyncPair<bool>，在入队前准备对应命令 ID 的完成状态，工作线程处理完成后通过 notify 发布结果；调用方在管理锁外调用 wait。完成状态不依赖 Cookie 的存活，也不保存调用方参数变量的引用。本线程同步增删不使用 SyncPair。
- 管理队列有容量上限，统计所有已接纳但尚未完成的入队命令，包括 commands_、batch_ 及正在处理的命令；缓存交换不释放名额，命令完成后才释放。本线程同步直接增删不占管理队列名额，但仍遵守处理器容量预留规则。
- 队列满载时，同步和异步增删都返回 false，不排队等待空位，不修改 Cookie，不遗留容量预留或同步记录。同步查询同样明确报告接纳失败，不伪造空快照。
- 每轮处理数量有正数预算，避免管理请求持续挤占业务；管理队列容量和预算的具体数值在实现、测试阶段确定。预算用尽后保留批次及处理游标，下轮先处理剩余批次，再取新命令，禁止交换覆盖未处理批次或让新命令越过旧命令。
- 交换管理命令缓存仅交换容器缓冲区，与移动 HandlerEntry 是不同操作。
- 命令入队、pending 发布、缓存交换与 pending 清除必须使用同一管理锁协议，避免丢命令；工作线程每轮主动检查 pending，不发送提前唤醒通知。
- Cookie 不作为外部读取 handler_index 的并发通道；定位由工作线程完成。

命令是否待处理由入队提示和工作线程的剩余批次共同判断；pending_ 的发布和清除不能使剩余批次被漏处理。停止及运行异常的收尾不再穿插业务轮询，按原顺序处理全部已接纳命令，不受正常轮次预算限制。

管理命令缓存的结构示意：

~~~cpp
SpinLock command_lock_;
std::vector<Command> commands_;
std::vector<Command> batch_;
std::atomic<bool> pending_{false};
~~~

命令入队和缓存交换使用 std::lock_guard<SpinLock>。Cookie、回调和同步状态尽量在加锁前准备；处理命令、销毁回调、SyncPair 等待/通知和线程 join 均在管理锁外进行。

工作线程按轮次检查停止标志、处理管理命令、固定本轮遍历数量并调用未标记处理器，轮尾清理标记删除后，再根据模式执行一种动作：kBusyPolling 执行 _mm_pause()，kIdle 执行 sleep_for(sleep_us)，之后进入下一轮。提交命令、本线程同步操作接纳或关闭接纳入口时，在同一个 command_lock_ 下更新相应状态，不通知工作线程。pause 和休眠均在管理锁外进行，休眠结束后工作线程自行检查命令和停止标志；已进入的休眠不被管理操作提前中断。自旋锁用于管理入口，不进入无管理操作的正常 polling 路径。

外部同步调用的通知使用 [SyncPair](../basic/async_result_syncer.h)，每个执行器持有一个实例。命令 ID 与 HandlerId 分离：同一处理器的入队同步添加、删除使用不同命令 ID，不复用已经消费的同步状态。异步命令和本线程同步直接路径不创建同步记录，也不调用 wait/notify。

~~~cpp
detail::SyncPair<bool> sync_pair_{1};

// 调用线程：命令 ID 和完成状态须在入队前准备好，入队失败时撤销。
// 命令成功接纳并释放管理锁之后，等待对应结果。
bool operation_ok = false;
bool received = sync_pair_.wait(operation_ok, command_id);
// received 表示收到结果；operation_ok 才表示增删成功。

// 工作线程：完成注册，或完成移除及回调资源清理后，通知一次。
sync_pair_.notify(command_id, operation_ok);
~~~

上述调用线程与工作线程代码为分别执行的流程示意，operation_ok 在两侧各为局部变量，命令只按值保存 command_id。同步增删采用不带超时的 wait，不能将超时等同于操作失败；已接纳操作仍可能继续执行。通知可以早于等待，结果须保留至调用方取走。SyncPair 的通知仅用于工作线程向同步调用方发布完成结果，不用于提前唤醒工作线程。

当前 SyncPair 尚不能直接作为已验证实现接入：wait() 的底层函数错误地返回 future::wait() 的 void 结果，wait_until() 的调用名与定义不一致。实现阶段必须修正这些问题，补充完成状态的入队前准备及入队失败撤销接口，并保证记录在通知者和等待者都不再访问后才能回收。reset/clear 不得破坏在途等待；正常完成后须回收记录。以上是接入要求，本次设计确认不表示这些修正已完成。

## 10. 生命周期、查询与错误

以下为已经确定的生命周期、查询和错误处理约束：

- 构造时接收并校验配置，不启动线程；Start 返回 bool，使用保存的配置启动工作线程并握手确认生效，成功返回 true；启动失败时完成本次线程收尾后返回 false，且不执行回调。
- 启动错误使用 StartError 保存错误码、失败步骤和描述，通过线程安全的 GetLastStartError() 返回 std::optional 快照；启动成功清空错误，失败在返回前更新错误。
- 运行中重复 Start 返回 true；启动失败后允许以原配置重试；一旦请求停止，Start 返回 false，不支持停止后重启。生命周期和管理入口同步保证并发启动、停止不重复创建或回收线程，也不重新开放已关闭的入口。
- RequestStop() noexcept 在同一管理锁下关闭接纳入口并设置停止标志，允许任意线程（包括本工作线程）调用；重复调用无额外效果，不等待线程退出，也不提前唤醒已进入的休眠。
- Stop() 返回 void，先请求停止，再等待当前回调或已进入的休眠自然结束，完成命令收尾、资源清理及工作线程退出后才正常返回。停止采用协作方式，不强制终止回调。
- Stop() 幂等，外部线程重复调用正常返回；并发 Stop 调用须串行完成线程回收，不能并发 join 同一工作线程。本工作线程调用 Stop 时，在等待或获取可能造成自等待的生命周期锁之前抛 std::logic_error；本线程停止使用 RequestStop。
- 停止时妥善清理已接纳命令、回调和预留名额，注销 Cookie 并释放内部 shared_ptr。外部 Cookie 裸指针随清理失效，已接纳添加不保证至少执行一次。
- 停止关闭接纳入口后，按顺序处理所有已接纳命令，再清理剩余处理器；每个已接纳同步命令必须向对应的 SyncPair 命令 ID 通知一次处理结果，不能留下永久等待的调用方。等待者退出前不能清空同步记录或销毁 SyncPair。
- 析构等待线程退出；不得在本执行器回调中析构执行器。
- 所有者负责协调执行器生命周期，不能在停止或销毁后继续使用 Cookie 裸指针。
- 处理器查询返回 HandlerId、名称的独立快照，仅包含已注册且未标记注销的处理器，不包含尚未注册或已取消的添加，不暴露 HandlerEntry 地址、callback 或可写位置。
- 线程查询返回保存的配置及名称、亲和性、调度策略、静态优先级和 nice 的实际生效值，不加入每次回调的正常执行路径。

### 查询路径

外部线程的处理器和线程查询通过同一管理队列同步提交，在工作线程执行查询命令时生成快照，遵守队列顺序和容量限制。本线程查询直接读取当前工作线程拥有的数据，快照反映调用时点；本轮直接添加已经注册，因此包含在快照内，已标记注销的条目则排除。查询不处理或等待其他队列命令，也不在管理锁内复制整份快照。

启动前、启动失败后及停止接纳入口关闭后的处理器和线程查询明确报告不可用；关闭入口前已接纳的查询仍按队列顺序完成。StartError 和 RuntimeError 查询独立于管理队列，在线程启动前或停止后仍可返回线程安全的诊断快照，执行器生命周期仍须由调用方协调。

### 运行异常

Handler 抛异常时，捕获 std::exception 及其他异常，使用 std::exception_ptr 保留原始异常，并保存对应 HandlerId。只保留首次导致停止的异常，后续收尾错误不能覆盖根因；不恢复调用出错的 Handler，也不进入旧草稿的 disabled 任务状态。

保存首次运行异常后，按停止协议关闭接纳入口并设置停止标志，不再调用本轮剩余 Handler。当前调用退出后先清理本线程标记删除的条目，再按队列顺序处理全部已接纳命令，为每个外部同步命令发布一次结果，最后清理剩余处理器、资源及名额并退出工作线程。执行器因运行异常停止后，同样不允许重启。

GetLastRuntimeError() 返回 std::optional<RuntimeError> 的独立快照；初始为空，错误记录和读取由内部同步保护，停止后仍保留原异常，可由调用方通过 std::rethrow_exception 查看。该接口不引入错误统计或观测对象。

参数错误、容量不足和停止后提交等可恢复的管理操作失败返回 false。管理命令处理中的可恢复异常应回滚本次操作，并向外部同步调用方发布失败结果，不能遗漏完成通知；无法继续安全执行的内部运行异常也应保存首次原因（没有对应 Handler 时 ID 为 0）并进入停止收尾。用户捕获对象析构不抛异常、不重入增删是资源清理的前提。

删除接纳、注销与资源回收必须区分。外部同步删除成功返回时可释放借用资源；本线程同步删除已注册处理器只保证注销，当前调用仍在使用的资源须保留至调用结束，捕获资源和名额在轮尾回收。取消尚未注册的异步添加则保证其回调不会执行，命令持有的捕获资源和名额在添加命令处理时回收，不能把捕获对象尚未析构等同于资源已经全部清理。异步删除不提供成功完成通知，不能仅凭返回 true 或 Cookie 置空释放借用资源；需要明确回收完成的调用方使用外部同步删除，或协调至 Stop 返回后再释放。不能通过 Cookie 引用计数、内部字段或已释放指针判断完成。本阶段暂不增加观测对象或错误统计查询接口。

## 11. 实现与验证事项

上述设计选择已确认。以下为实现和验证工作，不表示实现已经完成：

1. 修复 SyncPair，并补齐完成状态的入队前准备、失败撤销、单次通知和安全回收；同步查询的结果传递同样不得依赖调用方参数变量或 Cookie 的存活。
2. 通过负载和延迟测试确定管理队列容量、每轮处理预算的具体数值，验证满载拒绝、跨批次顺序、容量回滚和停止收尾。
3. 按第 3 节配置规则补齐平台校验，并验证线程配置回读、启动失败诊断及重试行为。
4. 验证异常退出、同步自删除、取消待注册添加、快照和资源回收的组合边界；捕获对象析构不得重入增删或抛异常。

## 12. 验收计划

实现遵循 Google C++ 风格，运行适用的 clang-format、clang-tidy、构建和测试。以下为计划，不是已完成结果。

功能与资源安全：

- 正常添加、空回调、512 个处理器满载，以及待处理添加的容量预留。
- 多线程跨实例分配 ID 唯一，执行器销毁重建不复用，ID 耗尽不回绕。
- 中间及尾部删除；移动处理器的 Cookie 地址和 ID 不变，下标正确更新。
- vector 大小和容量运行期间保持不变；轮内已标记条目保留资源且不调用，轮尾回收后有效前缀无空洞。
- 添加后立即异步删除、回调内异步增删、多生产者的队列接纳顺序；本线程同步直接操作可以先于已入队命令生效。
- 添加成功时设置 Cookie 输出参数，失败时保持原值，预留名额及资源正确回滚。
- 添加时 cookie 原值非空也正常处理，不读取原 Cookie；成功覆盖输出且原 Handler 继续存在，失败保持原值，不隐式删除或释放原处理器。
- 外部同步增删等待工作线程处理，添加成功时已注册，删除成功时回调及捕获资源已清理；显式传 true 的异步增删在接纳后返回，不等待被阻塞的当前回调，也不提供成功完成通知。
- 异步删除接纳成功、同步删除处理成功时在函数内将 cookie 置为 nullptr，失败时保持原值；置空不影响命令按值保存的指针，空指针输入安全返回 false。
- 仅同步增删检查调用线程身份；本线程同步添加直接注册且下一轮才调用，同步删除自己或其他 Handler 直接标记注销、置空 Cookie，不创建同步记录；异步增删无论调用线程均入队。
- 标记删除尚未调用的 Handler 时本轮跳过；删除自己后当前调用安全继续，回调及捕获对象在调用期间不移动、不销毁。轮尾连续或全部标记删除均正确清理，补入条目不会漏清理。
- 本线程同步删除后资源和名额在轮尾释放，满载时删除后立即添加仍拒绝；停止或回调异常退出同样执行标记清理。同步等待期间不持有管理锁。
- 本线程先异步添加、后同步删除尚未注册的处理器，删除返回 true 并置空 Cookie；添加命令处理时检查取消标志，既不注册也不调用回调，资源和预留名额只释放一次。命令仍按原队列顺序处理，不创建同步记录或发送成功通知。
- 取消待注册添加后立即添加仍须计算未释放名额；停止时同样处理已取消的添加命令并完成回收。外部同步删除待注册添加保持队列等待语义，不在外部线程访问取消标志。
- A 的工作线程调用 B 的同步增删必须走 B 的外部同步路径，不能误判为本线程；本线程直接路径不绕过停止接纳检查和预留容量协议。
- 管理命令入队、缓存交换和停止接纳使用同一 SpinLock；命令处理、资源清理和 sleep_for 在锁外进行，休眠期间入队的命令在后续轮次正确处理。
- 管理队列容量覆盖待取批次、剩余批次和执行中的命令，交换缓存不重复释放名额；满载增删返回 false 且 Cookie、处理器名额及同步记录正确回滚。
- 连续多轮达到管理预算时，旧批次先于新命令执行，业务轮询仍有执行机会；停止时无须继续业务轮询即可按序完成所有已接纳命令。
- 同步添加失败不遗留名额；同步删除校验失败不影响其他处理器，也不置空参数。
- SyncPair 的先通知后等待、先等待后通知均正确；收到 false 结果与未收到结果可区分，多条同步命令的 ID 及结果互不串扰。
- 同一处理器入队的外部同步添加和删除使用不同命令 ID，每条命令只通知一次；完成记录安全回收，入队失败撤销预备状态。
- 输出裸指针与内部 shared_ptr 指向同一个 Cookie，移动条目不改变地址；外部不拥有 Cookie。
- 删除、停止正确注销 Cookie 并释放内部引用，最后一个引用释放时才销毁 Cookie，回调及捕获资源正确释放。
- 空及错误归属的有效 Cookie 被拒绝；调用方不并发、重复提交同一 Cookie 的删除，不使用已释放 Cookie。
- 停止清理前完成已接纳删除命令；外部指针置空不隐式删除处理器，停止后不再使用裸指针。
- 停止与同步增删并发时，已接纳命令均发布处理结果，关闭接纳后的请求返回 false，不遗留等待者。
- 管理命令和停止请求不提前中断已进入的休眠；休眠结束后完成未处理命令收尾，停止及析构等待线程退出。
- RequestStop 在本线程或外部线程均可调用，关闭接纳后不等待线程退出，重复请求无额外效果；Stop 正常返回时线程已退出、资源及名额已清理，外部重复和并发调用均不重复 join。
- 本工作线程调用 Stop 明确抛 std::logic_error，不阻塞在生命周期锁或等待自身；回调内调用 RequestStop 后自然返回，外部 Stop 能完成收尾。
- 线程名称、亲和性、优先级生效及权限不足、配置无效等失败路径。
- Start 成功返回 true 时线程已启动且配置生效；线程创建、设置或生效校验失败时返回 false，本次启动线程已收尾且未执行回调。
- GetLastStartError() 初始为空，启动失败时包含正确错误码、失败步骤和描述，启动成功时为空；返回副本不影响内部记录，并发更新与查询没有数据竞争。
- 运行中重复 Start 返回 true，线程及配置设置不重复执行；失败后的重试使用原配置新建线程并完整握手，先前失败线程不遗留。
- RequestStop 或 Stop 后的 Start 返回 false 并记录原因，包括启动前就请求停止的情况；并发启动、停止不重新开放接纳入口，不重复创建或回收工作线程。
- 默认配置采用普通调度 SCHED_OTHER，实际系统静态优先级为 0；kOther 下忽略 priority、不执行实时等级校验，改变该字段不影响普通调度行为。实时调度仅在显式配置时启用。
- 实时绑核模式的五档优先级分别映射为 1、30、50、70、99，默认 kMedium 映射为 50；非法枚举值在构造时拒绝，并校验系统调度策略的有效范围。
- 普通调度下 NicePriority 的最低、较低、较高、最高分别映射为 nice=19、10、-10、-20；默认 kMedium 不调用 setpriority，保留继承值，包括继承值非零的情况。
- kOther 下拒绝非法 nice 等级，显式 nice 设置失败时报告启动错误；实时调度忽略 nice_value，不校验其等级，也不设置 nice。
- kBusyPolling 每轮遍历结束后只执行一次 _mm_pause()；kIdle 只执行 sleep_for(sleep_us)，无处理器时同样按对应模式处理，kIdle 下非正休眠时长在构造时拒绝。
- cpu=-1 时不设置绑核，保留继承的亲和性；cpu=0 及其他可用编号能正确绑定并回读，cpu < -1 在构造时拒绝，不可用或不受支持的非负编号在启动时明确失败。
- 构造时保存独立配置且不启动线程，修改调用方原配置不影响执行器；无效字段在构造时报告，操作系统设置失败在启动时报告。
- 外部同步删除完成先于借用资源释放，本线程标记注销不冒充资源回收完成；查询与位置更新不存在数据竞争，快照不包含已注销条目。
- 外部查询在命令执行时生成快照，本线程查询立即完成且不等待队列；快照包含本线程已注册的新条目，排除标记删除、待注册和已取消添加，调用方修改快照不影响执行器。
- 普通查询在启动前、启动失败后或关闭接纳后明确失败，已接纳查询在停止时完成；启动和运行诊断查询不依赖工作线程仍在运行。
- Handler 抛标准或非标准异常均保存原始异常及正确 HandlerId，停止调用后续 Handler；当前回调安全退出、标记条目清理、队列收尾和同步结果发布均完成，Stop 可正常回收线程。
- 运行异常快照初始为空，保留首次致停异常且后续错误不覆盖，并发读取无数据竞争，停止后可重新抛出原异常；可恢复管理异常回滚并通知外部同步调用方失败。

性能：

- 不同处理器数量下的 polling 吞吐和轮次时延。
- 增删、查询对业务处理 p50/p99/p99.9 延迟的影响。
- 不同管理队列容量和每轮预算下的吞吐、满载拒绝率及业务与管理命令尾延迟。
- 命令接纳延迟和生效延迟分别测量。
- kBusyPolling 和 kIdle 的 CPU 占用与响应时间，以及不同微秒休眠配置下的行为。
- 捕获对象大小和析构成本对删除时延的影响。

运行相关测试及 ASan/UBSan，环境允许时运行 TSan。没有业务负载、硬件和测量结果时不承诺固定纳秒或微秒指标。

## 13. 参考

- [Folly 与 Google 线程方案调研](threading_research.md)：前期源码核对记录。其中同步删除和任务状态等早期建议不覆盖本文的当前设计。
- [pthread_setaffinity_np](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html)：线程亲和性。
- [pthread_setschedparam](https://man7.org/linux/man-pages/man3/pthread_setschedparam.3.html)：策略和优先级。
- [Linux 调度说明](https://man7.org/linux/man-pages/man7/sched.7.html)：调度语义和权限。
- [Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_pause)：_mm_pause 自旋提示。

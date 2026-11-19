# TCP 有向流管理：插入后立即处理

链接 `tcp_reassembly`，包含 `tcp_stream_manager.h`。管理器接收已解析的
IPv4/TCP 报文，按 `domain_id + (src_ip, src_port, dst_ip, dst_port)` 管理
独立单向重组器。地址、端口为主机字节序。正反向不合并，由用户分别配置规则
并保存两个方向的句柄／重组器指针。

## 一次 insert 完成当前流处理

```cpp
net::tcp_stream_manager manager({4096, 64 * 1024, true});
const net::tcp_four_tuple forward{{0x0a000001, 12000}, {0x0a000002, 9000}};
manager.replace_rules({{1, forward}});

auto result = manager.insert(
    packet,
    [&](const net::tcp_stream_context& context,
        const net::readable_buffers& data,
        net::tcp_stream_status status) -> size_t {
      size_t consumed = parser_for(context.handle).parse(data, status);
      return data.size() - consumed; // 未消费后缀的长度
    },
    [&](const net::tcp_stream_context& context,
        const net::tcp_managed_stream_state& state) {
      record_state(context.handle, context.tuple, state);
    });
```

带两个回调的 `insert` 在匹配、插入成功后立即调用当前流的重组器 `process`。
一次四元组定位，不再为处理重新查句柄，也不选择其他流。回调对象仅在本次
调用栈上借用，返回即结束，不保存、不分配 `std::function`。

`result.status == handled` 时 `result.context` 为该流的 `tcp_stream_reassembly*`。
`result.stream` 为插入结果，`result.processing` 为本次消费结果。
重复报文及已归属的空 SYN/ACK/FIN/RST 也会处理当前流；没有连续数据时不调用
数据回调，但状态回调仍恰好调用一次。窗口拒绝、过滤、满表、非法输入、已结束
记录拒绝和捕获结束等失败不调用任一回调，context 为 null。

数据回调最多调用一次，获得逻辑连续、物理最多两段的视图；返回未消费后缀量
必须在 `[0, data.size()]`。返回过大时不消费，processing.status 为
invalid_remaining，但仍报告状态。部分消费后不循环处理、不自动安排再次调用。

管理层的处理只是转发：流身份上下文和管理元数据保存在记录中，状态回调时
更新其中的单向重组状态。数据交付、消费和位图清理均在重组器内完成。
没有就绪队列、process_ready、重新入队、批量调度或终止通知确认标记。

## 单独插入，外部直接处理

不带回调的 `insert(packet)` 仅插入，返回同一个重组器指针：

```cpp
auto result = manager.insert(packet);
if (result.context != nullptr) {
  result.context->process(
      [&](const net::readable_buffers& data,
          net::tcp_stream_status status) -> size_t {
        return parser.parse_remaining(data, status);
      },
      [&](const net::tcp_stream_state& state) {
        record_reassembly_state(state);
      });
}
```

直接调用使用重组器原生两个回调，不增加管理层上下文或生命周期包装。
连接管理信息按需 `manager.state(*result.handle)` 查询。FIN/RST、撤销规则或
finish_all 的管理状态保存在对应记录中；原生 tcp_stream_state 仅表达字节重组
状态，直接处理时需要用户按需查询结束原因。

指针是借用，不转移所有权。记录存活期间地址稳定，正反向及不同 domain 拥有
不同指针；release 后指针立即逻辑失效，即使槽位复用使地址相同也禁止使用旧
指针。句柄使用 slot + generation，旧句柄不能访问或释放新记录。外部只通过
指针 process／查询，插入、清理和对象寿命由管理器负责，禁止 delete 指针。

保留 `manager.process(handle, on_data, on_state)` 作为按句柄手动处理的入口，
它只做句柄校验及相同回调转发，用户自行选择调用时机。缓冲窗口拒绝后，可用
已有指针或句柄先消费，再重试 insert。

## 过滤、记录和资源

规则为 `tcp_match_rule{rule_id, tuple}`，四个字段全部精确匹配且保留方向。
0 地址、0 端口为精确值；空规则集拒绝所有新流，不提供通配或自动反向规则。
需要两个方向时显式配置 `{1, forward}` 与 `{2, {forward.dst, forward.src}}`。

每条记录只有一个重组器，max_connections 包括尚未释放的终止记录。
满表返回 connection_limit，不淘汰旧流。首次使用槽位构造重组器，两次
posix_memalign 分别申请 64 字节对齐的裸指针 buffer_、bit_map_；部分失败清理
已申请内存，不发布新记录。容量必须为 `[64, 2^31)` 内的 2 的幂，初始化后
不扩容。每槽负载及位图占 `capacity × 9/8` 字节，另加元数据和哈希表。
释放后保留缓存供槽位复用，管理器析构时归还系统。

domain 用来隔离不同网卡、VLAN、命名空间等相同四元组；过滤规则本身只看
四元组。IP 分片重组、pcap/网卡读取、头长、校验和和捕获长度验证属于入口层，
不得把截断负载当完整报文传入。同一管理器单线程持有，外部必须串行使用。

## 插入与结束观察

- 匹配规则的 SYN（包括 SYN+ACK）或允许中途接入时的非空负载可建立记录。
  未知纯 ACK、纯 FIN、RST 不建立记录。allow_midstream 对每个方向独立生效。
- SYN 负载序号为 seq+1，空 SYN 建立起点。中途捕获以首个合法负载为基准，
  started_midstream 永久保留；迟到 SYN 不回退已选定的起点。同 ISN 为重传。
- 同有向键出现不同已知 ISN 的 SYN，先创建新记录，成功后才将旧记录标记
  replaced。满表或构造失败保留旧记录，旧句柄由用户另外处理和释放。
- 成功接受 FIN 后标记 terminated/fin_observed，终点不含 FIN；有缺口也立即
  通过带回调 insert 报告。该方向在释放前可接收终点前迟到数据。它不证明
  整个双向 TCP 连接关闭或捕获数据完整。
- RST 只更新其有向记录，忽略该 RST 自带的负载，已缓存连续数据仍可处理。
  反向 FIN/RST/ACK 不更新本方向；关联两个方向由用户负责。
- last_acknowledgment 仅为本方向携带的 ACK 观察，不推断反向 FIN 已获确认。
- 乱序、重复和重叠由重组器负责，重叠保留首次内容。插入拒绝原子保留字节
  和终点，管理器仍可更新活动时间、独立 ACK 观察及拒绝计数。

这些是抓包观察，不实现完整主动 TCP 栈。缺失连接首尾、四元组复用后的旧包
等信息不足情况无法完全推断。管理器不执行超时检查、不维护 LRU，也不自动
回收空闲记录；时间戳仅保存单调水位下的活动信息。

## 规则撤销与显式释放

replace_rules 在控制路径编译新规则并重评估现有记录。重复 rule_id 拒绝整次
更新；构建异常向外传播，两者均保留旧规则。哈希精确匹配均摊 O(1)，ID 排序
为 O(R log R)，重评估和处理中状态校验扫描槽位。规则更新不得与处理并发。

**撤销规则时立即清空该记录的缓存并标记 filter_removed**，使已借出的重组器
指针也无法继续交付被撤销负载。丢弃发生在规则更新阶段，不在 process 中。
更新结果 discarded_bytes 报告本次总丢弃量，各记录 state.discarded_bytes
累计该原因丢弃量。正反向独立撤销；重新放开规则不复活旧记录。

这是外部直接持有重组器后的策略调整：过去由管理层 process 拦截交付，现在
由撤销操作丢弃并报告。replace_rules 不调用回调，业务按持有的句柄查询或
手动 process 读取结束原因。

永久缺口由业务根据 stream.gap 的缺失范围、后续连续量和缓存量判断。回调
返回后重置业务解析状态，再 skip_gap(handle)，保留缺口后的有效数据；操作
不自动回调，也不安排后续处理，由用户再次调用 process。

EOF、捕获错误或人工结束时 finish_all(capture_end/capture_error/manual) 扫描
槽位并标记活动记录结束，停止新输入，保留已有终止原因，不调用回调、不清空
其负载。用户根据已保存的句柄或重组器指针逐条处理、查询和释放。

release(handle) 要求记录已终止；有缓存时默认 buffered_data，允许继续消费，
或者显式 discard_buffered 并返回实际丢弃量。**通知是否已处理由调用方负责，
不再要求内部 terminal_notified 标记**，原生重组器处理后也可正常释放。
空缓存且有缺口也允许释放，业务须自行决定是否继续等待迟到数据。

## 异常和回调边界

数据回调异常不消费本次数据，不执行本次状态回调；状态回调异常保留已完成
消费。异常向外传播，不回滚成功插入，不自动重试或调度。首次 insert 回调
抛出时返回值不可用，可重试相同报文取得现有记录，已消费数据不会再次交付。

管理器回调中可查询 state/stats，禁止重入管理器变更／处理接口，禁止销毁
管理器。直接使用原生重组器时，其自身防止 process/insert/clear 重入；管理器
也会在释放本记录、插入本记录或全局规则更新／结束前检查正在处理的重组器，
防止清理回调正在使用的内存。输入指针在 insert 返回后可复用，回调参数和视图
仅在回调期间有效。

## 接口迁移与验证

- 新增 insert(packet, on_data, on_state)，同步处理当前流。
- insert 结果新增 context（原生重组器借用指针）与 processing（消费结果）。
- 移除 process_ready、批量处理结果、ready_streams 和内部就绪队列。
- 移除 terminal_notified、notification_pending；释放由调用方显式协调。
- 管理快照保存于记录中，process 不逐次构造整份管理快照。
- 规则撤销的丢弃量由结果和记录统计报告。
- 配置保持 `{max_connections, stream_capacity, allow_midstream}`，无超时配置。

```sh
cmake -S . -B build
cmake --build build --target test_tcp_stream_manager test_tcp_stream_reassembly -j 2
ctest --test-dir build -R '^test_tcp_stream_(manager|reassembly)$' --output-on-failure
```

测试覆盖严格方向匹配、中途接入、乱序重叠、控制结束、句柄复用、立即处理
目标流、原生指针处理、失败无回调、部分消费、异常重试、撤销后不暴露缓存、
回调期间释放／全局重配置保护、随机多方向交付和对齐分配故障清理。

## 基准

```sh
cmake -S . -B /tmp/tcp-manager-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build /tmp/tcp-manager-release --target benchmark_tcp_stream_manager -j 2
/tmp/tcp-manager-release/network/common/benchmark_tcp_stream_manager
```

逐包场景使用新的 insert(packet, callbacks)；批量场景先插入再显式调用当前
句柄的 process。有序负载 256 字节、容量 32768；其他场景容量 4096。状态回调
为空，数据回调消费全部。测量范围包含模块处理，网卡、入口解析及实际业务
回调需另外测量。批次均摊 P99 不代表逐包尾延迟；此前就绪队列版本的数值
不能直接当作新接口的逐包时延。

本环境（Intel Core i7-10510U，Clang 22.1.2，Release，单线程、未绑核）一次
运行结果如下。逐包调用包含计时开销，其他场景按表中批量单位均摊；这些是
空业务回调下的观测值，受机器调度和频率影响。

| 操作／样本单位 | 平均 ns | P99 ns |
| --- | ---: | ---: |
| 逐包 insert(packet, callbacks)，含当前流处理 | 134.7 | 209.0 |
| 插入后手动 process，64 包按包均摊 | 93.9 | 305.8 |
| 乱序、重复、重叠，4 包按包均摊 | 122.7 | 179.0 |
| 4096 条精确规则拒绝，每批 64 次均摊 | 50.8 | 73.6 |
| 首次 SYN 插入，含两次对齐分配 | 2362.7 | 7266.0 |
| 槽位复用的 SYN/RST/process/release 整次生命周期 | 250.2 | 327.0 |

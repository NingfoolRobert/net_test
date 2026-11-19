# TCP 单向字节流重组

链接 CMake 目标 `tcp_reassembly`，包含 `tcp_stream_reassembly.h`。
每个连接方向使用一个 `net::tcp_stream_reassembly`，由同一个线程持有。
本模块接收已解析的负载，不解析 pcap、IP/TCP 头，不管理连接或超时。

```cpp
net::tcp_stream_reassembly stream(1024 * 1024);

auto inserted = stream.insert(
    {{payload_sequence, {payload, payload_size}}, std::nullopt});

// 可以先插入多个包，再统一处理。
auto processed = stream.process(
    [&](const net::readable_buffers& data,
        net::tcp_stream_status status) -> size_t {
      const size_t consumed = ParseCompleteMessages(data.first, data.second);
      return data.size() - consumed;
    },
    [&](const net::tcp_stream_state& snapshot) {
      // snapshot.status 为状态码；snapshot.gap 有值时含缺口进度。
      RecordState(snapshot);
    });
```

首个合法 `insert()` 自动以负载序号建立起点，不需要先 reset。若先观察到
SYN，上层可先提交空负载、序号为 `SYN.seq + 1` 的输入，再接收乱序负载。
校验失败不建立起点。`clear()` 用于复用实例，清空后由下一次合法插入重新定起点。

回调状态码为：`uninitialized=0`、`waiting_data=1`、`data_ready=2`、
`gap_waiting=3`、`end_received=4`。有缺口优先报告 `gap_waiting`，即使当前
回调有缺口前的连续数据；无缺口且到达已知终点时优先报告 `end_received`，
否则依据连续数据量区分 `data_ready` 和 `waiting_data`。数据回调获取消费前
状态，状态回调获取消费后快照。`state()` 可随时查询完整快照和跳过缺口的历史。
这些是重组状态，不代表 TCP 连接状态机或是否观察到握手。

## 插入与业务处理分离

`insert(input)` 只校验、重组并更新状态，不保存或调用业务回调，不消费数据。
返回结果仅含插入状态、新增字节数和重叠字节数。`skip_gap()`、`clear()` 也不
触发回调。真正的永久丢失由上层判断，模块检测的是当前可确认的缺口。

`process(on_data, on_state)` 是唯一调用回调的入口：先最多调用一次数据回调，
按返回值消费前缀，再调用一次状态回调。每次 `process()` 都报告当前状态，
包括尚未插入时的 `uninitialized`、没有可读数据或状态未改变的情况，不做通知
去重。正常返回时，处理结果的 `state_notified` 总为 `true`。数据回调抛出异常
时本次处理直接退出，不再调用状态回调；状态回调自身的异常也向外传播。

通知采用最新快照，不维护事件队列。两个 `process()` 之间出现又补齐的缺口
不会逐个回放。需要观察每个报文的状态变化时，在每次插入后调用 `process()`。
批量大小和调用频率由上层决定，并受固定容量限制。接口分离不代表线程安全，
同一实例的插入、处理和清理仍必须由单线程或外部同步串行执行。

## 缓存与消费

- 容量必须是不小于 64 的 2 的幂，且小于 `2^31`，非法容量抛出
  `std::invalid_argument`。
- `buffer_` 和 `bit_map_` 为模块拥有的裸指针，构造时分别通过
  `posix_memalign` 按 64 字节对齐申请，析构时 `free`。第二次申请失败时释放
  第一次申请的内存。内存不足抛出 `std::bad_alloc`，其他 POSIX 错误以
  `std::system_error` 报告。容量不变，重置复用内存。
- 负载缓存额外需要容量的 `1/8` 作为位图。只交付有效位已设置的数据，
  原始数据区不会为重置而清零。
- 一次插入先校验全部输入，再保存新增字节。越界整次拒绝，不部分接收，
  不写缓存，不记录本次终点。可以先 `process()` 消费，再重试。
- 已输出或已缓存的重复字节不再次保存。重叠保留首次捕获内容，不比较冲突
  内容；`overlap_bytes` 包含已消费的重传字节。
- `on_data` 每次最多调用一次，提供逻辑连续、物理上最多两段的数据。
  返回值是未消费后缀长度，必须在 `[0, data.size()]` 内。返回全部长度不会
  忙循环；可以在没有新报文时再次使用 `process(on_data, on_state)`。
- 返回值过大时，处理结果的 `status` 为 `invalid_remaining`，数据仍保留，
  消费位置不前进。回调异常向外传播；数据回调抛出时不消费任何本次提供的字节。
  业务副作用不能由本模块回滚。
- 输入指针在 `insert()` 返回后可复用。输出视图只在回调期间有效，禁止异步
  保存。回调可查询 `state()`，但不能重入 `insert/process/skip_gap/clear`；
  重入抛出 `std::logic_error`。调用方也不能在回调中销毁当前实例。

## 缺口和结束序号

状态快照的 `gap` 报告最靠前的阻塞缺口，包含范围、后续连续量和累计缓存量。
没有任何已知缺口时 `gap` 为空。状态通知回调抛出后，下次 `process()` 仍照常
报告当前状态；已完成的数据写入、消费或跳过不会回滚。

上层从 FIN 得到负载结束序号后，可随负载或空负载提交 `end_sequence`。
该值是不包含 FIN 的负载结束位置，可揭示缺口后没有数据的尾部缺口。
首个空负载也会以 `payload.sequence` 建立起点；建立后，空负载的该字段不用于
重新定位。矛盾、倒退或恰好相差半个序号空间
的终点被拒绝。模块不识别 FIN/RST，也不发送连接关闭通知。

`state().end_received` 表示从本实例起点到终点的数据均已收到或已被显式跳过，
不表示业务已消费完毕；应结合 `readable_bytes` 和 `had_skipped_gap` 判断。
中途抓包不能据此声称整个真实连接完整。

永久丢包由上层决定，不会自动超时或跳过。业务在当前操作返回后确认跳过，
先重置自身消息解析状态，再调用 `skip_gap()`。返回结果分别统计缺失
字节和被丢弃的未消费连续前缀。缺口后缓存仍保留，随后使用 `process()`；只有
已知结束终点的尾部缺口可以直接跳到终点。迟到报文不能回退已跳过的位置。
TCP 包头不能作为业务消息边界，恢复消息同步由业务协议负责。

## 从 TCP 流中途开始抓包

不要求先收到 SYN。首个合法插入自动建立起点。
该起点可能处于一条业务消息中间，业务解析器需要丢弃
无法识别的开头并验证下一个消息边界；模块不会自行寻找消息头。

低时延模式选定起点并开始交付后，不回退接收窗口：以后到达的更早序号被
当作旧数据裁剪，仅接受超过已消费位置的新增尾部。如果首个抓包本身可能
乱序且业务要求保留其前面的数据，上层应在初始化本模块前进行有界的启动
暂存，再选定起点；这一策略增加启动等待，不由单向重组模块自动执行。
上层也应记录“未观察到连接开头”，不能将本实例无缺口理解为整个连接完整。

## 验证

```sh
cmake -S . -B build
cmake --build build --target test_tcp_stream_reassembly -j 2
ctest --test-dir build -R '^test_tcp_stream_reassembly$' --output-on-failure
```

测试覆盖序号和环形回绕、重复/冲突重叠、部分消费、缺口进度与跳过、终点
校验、异常和重入；以逐字节参考模型随机对照。Linux 下额外用链接器包装
注入两次 POSIX 分配的失败，验证清理和错误码，并检查接收和重置不重新申请
这两块缓存。实际吞吐量和延迟仍需在目标机器及业务回调下测量。

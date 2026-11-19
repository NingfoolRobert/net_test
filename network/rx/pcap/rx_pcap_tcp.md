# 离线 pcap TCP 接收

链接 CMake 目标 `pcap_rx`，包含 `rx_pcap_tcp.h`。接收器读取未分片的 IPv4/TCP，
通过已有 `tcp_stream_manager` 重组单向 TCP 字节流。IPv4 分片直接跳过，不进行
IP 分片重组；首片、中间片和末片均不交给 TCP 管理器。支持 Ethernet
（含最多两层 VLAN）、RAW、Linux SLL 和 SLL2。输入应属于一个捕获域；VLAN ID
和 SLL2 接口号不参与流的隔离。同文件存在相同地址/四元组的多个
网络域时，应先按域拆分文件。

```cpp
net::rx_pcap_tcp receiver;
net::rx_pcap_tcp_cfg_t config;
config.file_path = "capture.pcap";
// 所有地址和端口使用主机字节序；四个字段均精确匹配，0 不是通配符。
config.rules = {{1, {{0x0a000001, 12000}, {0x0a000002, 9000}}}};
config.verify_checksums = true;
receiver.set_config(config);
if (!receiver.open()) {
  report_error(receiver.error());
  return;
}

char buffer[4096];
net::tcp_stream_context context;
int count;
while ((count = receiver.read_from(context, buffer, sizeof(buffer))) > 0) {
  consume(context, buffer, count);
}
if (count == -1) report_error(receiver.error());
```

`read_from` 每次只返回一个方向的连续数据，不保留 TCP 包边界。小目标缓冲区
不会截断丢弃后缀，下一次读取先继续交付后缀。`read` 使用相同逻辑但省略流身份，
适合单方向规则。返回正数表示字节数，`-2` 表示捕获 EOF，`-1` 表示错误。
多方向必须分别配置规则；空规则拒绝所有流。配置在下次 `open` 生效。

默认允许中途接入，首个合法负载选择流起点，无法恢复捕获之前的数据。捕获到
SYN 时可正确等待起点之后的乱序数据。缺口不自动跳过，EOF 也不意味着所有
TCP 数据已收齐。重传、重叠、FIN/RST 和序号回绕遵循现有 TCP 管理器契约。

需要控制包通知、部分消费或连接生命周期管理时，使用 `dispatch`：

```cpp
int result = receiver.dispatch(
    [&](const net::tcp_stream_context& context,
        const net::readable_buffers& buffers,
        net::tcp_stream_status status) -> size_t {
      return buffers.size() - parser_for(context.handle).consume(buffers, status);
    },
    [&](const net::tcp_stream_context& context,
        const net::tcp_managed_stream_state& state) {
      remember(context.handle, state);
    });
```

每次 `dispatch` 读取一个完整且可解析的 TCP 报文，返回 `1`（包括过滤、纯控制包、
未知 ACK 等情况）、`-2` 或 `-1`；`last_result()` 提供该包的管理器接收结果。
数据回调返回未消费后缀长度，状态回调在接收成功时调用。回调不存储，异常向外
传播。回调异常后该捕获包已经读取，已插入的数据保留；应在回调中保存流句柄，
通过管理器 `process` 继续处理。回调中禁止重入或关闭接收器。

`dispatch` 的未消费数据由调用方通过 `streams()->process(handle, ...)` 处理。
从 `read_from` 切换到 `dispatch` 前必须读完短读后缀，否则抛出 `logic_error`。
本接口没有自动调度或隐式丢弃。

`streams()` 在打开后返回管理器指针，直到下次 `close/open` 有效。调用方保存
已观察到的句柄，并通过管理器进行 `state/process/skip_gap/release`。终止记录
**不会自动释放**，也计入 `max_connections`；持续处理多个连接时应使用
`dispatch` 观察终止状态，并在回调返回后显式释放已经处理完的记录。FIN 带缺口
时可继续等待迟到数据，不应提前释放。EOF 和捕获错误调用 `finish_all`，保留
负载和已有终止原因，不额外调用状态回调；之后按保存的句柄查询和处理。

截断报文、非法头长/控制标志、所有 IPv4 分片、校验和错误（检查开启时）被跳过。
IPv4 的 MF 标志与分片偏移必须都为零，DF 标志允许置位。
TCP 连接容量或序号窗口拒绝返回 `-1` 并提供 `error()`，该包已经读取且不会自动重试。
`verify_checksums` 默认为 `false`，以兼容校验和卸载抓包；开启后校验每个 IPv4
头及 TCP 伪首部和报文校验和。当前不支持 IPv6 或实时抓包。

验证：

```sh
cmake -S . -B build
cmake --build build --target test_rx_pcap_tcp -j 2
ctest --test-dir build -R '^test_rx_pcap_tcp$' --output-on-failure
```

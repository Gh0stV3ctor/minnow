# minnow 架构文档

> 本文回答：这个项目整体是什么、分几层、数据怎么流、每个模块干什么、以及几个关键设计决策"为什么"这么定。
> 它是"大图"；每个函数的前置条件/不变式等细节留在各头文件的注释里。

## 1. 概述

minnow 是 Stanford CS144（Winter 2024）的实验仓库：**从零手写一个能真实互通的 TCP/IP 协议栈**。课程把它拆成 7 个 checkpoint，自下而上逐层实现：

| checkpoint | 模块 | 层 |
| --- | --- | --- |
| 0 | ByteStream（+ webget 热身） | 传输层地基 |
| 1 | Reassembler | 传输层 |
| 2 | WrappingInt32 + TCPReceiver | 传输层 |
| 3 | TCPSender | 传输层 |
| 4 | TCPPeer / TCPMinnowSocket（tcp_ipv4） | 传输层整合 |
| 5 | NetworkInterface（Ethernet + ARP） | 链路层 |
| 6 | Router（IP 转发） | 网络层 |

目标不是模拟器，而是**正确性足够、能与真实网络互通**的协议栈：check4 的 `tcp_ipv4` 能和 Linux 内核 TCP 对话，check5/6 收发的是真实格式的 Ethernet 帧。

## 2. 目录结构

```text
minnow/
├── src/      你要实现的协议栈模块（每个 checkpoint 补一两个文件）
├── util/     课程提供的基础库（Socket、Address、EventLoop、Parser、TUN…），不要改
├── tests/    测试框架（common.hh）＋ 各模块的测试用例
├── apps/     可执行入口：webget、tcp_native、tcp_ipv4
├── etc/      CMake 配置片段（build_type / cflags / scanners / tests）
├── scripts/  辅助脚本（并行 make、TUN 设备、行数统计）
└── writeups/ 每个 checkpoint 的实验报告模板
```

依赖方向是单向向下：`apps`/`tests` → `src` → `util`。`src` 不反向依赖上层。

## 3. 构建与测试体系

顶层 `CMakeLists.txt` 引入 `etc/*.cmake` 四个配置片段，再 `add_subdirectory` 挂上 util/src/tests/apps 四个子目录。

**同一份源码编三个变体**（关键设计）：

| 变体 | 编译选项 | 用途 |
| --- | --- | --- |
| `*_debug` | 严格告警 + `-Werror` | 日常开发 |
| `*_sanitized` | ASan + UBSan | 功能测试（抓内存/UB 错误） |
| `*_optimized` | `-O2` | 速度基准（测真实性能） |

**为什么编三份**：正确性测试要 sanitizer 当场抓 bug，但 sanitizer 太慢不能测性能；性能测试要 `-O2`，但优化版不便调试。所以分开。

**测试调度**：

- `etc/tests.cmake` 一次性注册全部测试名，并定义 `check0`…`check6` 快捷目标（本质是 `ctest -R 正则`）。
- `tests/CMakeLists.txt` 逐 checkpoint 增加实际测试可执行文件。
- 功能测试跑 `_sanitized`，速度测试跑 `_optimized`。
- `--stop-on-failure` 让 ctest 遇错即停（所以 webget 联网超时会截断后续测试）。

## 4. 数据流

### 接收方向（网络 → 应用）

```text
TunFD.read（Ethernet 帧字节）
  → EventLoop（poll 发现可读，触发回调）
  → NetworkInterface（Parser 解析帧头，按类型分派）
  → TCPReceiver（Wrap32 还原绝对索引，喂 Reassembler）
  → Reassembler（乱序重组）
  → ByteStream（入站缓冲）
  → 应用 read
```

### 发送方向（应用 → 网络）

```text
应用 write
  → ByteStream（出站缓冲，流量控制）
  → TCPSender（分段、编号、超时重传）
  → NetworkInterface（ARP 解析 MAC，封装成帧）
  → TunFD.write
```

每层只做一件事：**把一种"脏东西"（原始字节 / 乱序片段 / 32 位序号 / poll 等待）藏进内部状态，用一个干净接口交给下一层。** 应用数据这层"馅"从头到尾不变，每层只是加/剥一层头。

## 5. 各模块职责与关键设计

### ByteStream（check0）
内存中的有界 FIFO 字节流，用容量上限做流量控制。

- 接口：`Writer.push/close/available_capacity`，`Reader.peek/pop/is_finished`。
- 不变式：`bytes_buffered == bytes_pushed - bytes_popped ≤ capacity`。
- **关键设计：Writer/Reader 是"视图"，不是成员。** 状态全在 ByteStream 基类；Writer/Reader 是零数据子类，`writer()`/`reader()` 用 `static_cast<Writer&>(*this)` 返回同一个对象的两种类型视图。`static_assert(sizeof 相等)` 保证这个 downcast 安全。这样"生产者只能写、消费者只能读"，由类型系统强制。

### Reassembler（check1）
把带绝对索引、可能乱序/重复/重叠的片段重组成连续字节流。

- 接口：`insert(first_index, data, is_last)`、`bytes_pending()`。
- 状态：`next_index_`（下一个要写的索引）、`std::map<uint64_t,string> pending_`（按索引排序、保持互不重叠）、`eof_index_`。
- 算法：丢弃已写字节 → 丢弃超容量字节 → 合并进 pending → 连锁 push 连续字节。
- **为什么用 map 不用数组**：乱序片段是稀疏、随机的，map 能按索引有序地找到"下一个能接上的片段"，数组则要开大且大量空洞。

### WrappingInt32（check2）
在 32 位会回绕的 TCP 序号与 64 位绝对序号之间换算。`wrap` 取模 2^32，`unwrap` 用 checkpoint 就近还原。

### TCPReceiver（check2）
接收 TCP 段，把序号还原成绝对索引交给 Reassembler，并回 ACK + 窗口。它是"协议层"与"重组层"之间的桥，两者靠 `insert(绝对索引, payload, FIN)` 解耦。

### TCPSender（check3）
从出站 ByteStream 读数据、切段、编号、发送，维护未确认段队列，超时重传（RTO 指数退避），受接收窗口限制。

### TCPPeer / TCPMinnowSocket（check4）
把 sender + receiver 接成一个完整连接端点，跑在 TUN 网络上，能与内核 TCP 互通。

### NetworkInterface（check5）
IP 数据报 ↔ Ethernet 帧的双向转换 + ARP（IP→MAC）。发 IP 查 ARP 表，不知道就发 ARP 请求并暂存；收帧按类型分派。

### Router（check6）
多接口 IP 转发：最长前缀匹配 + TTL 递减。

## 6. util 基础设施（封装的核心）

每个 util 模块都是"脏底层 → 内部状态 → 干净接口 + 一个核心技巧"：

| 模块 | 脏底层 | 核心技巧 |
| --- | --- | --- |
| FileDescriptor | int fd | shared_ptr 引用计数，自动 close（RAII） |
| Address | sockaddr + DNS | getaddrinfo 一次解析，隐式转 sockaddr* |
| Socket/TCPSocket | socket 系统调用 | CheckSystemCall 把 -1/errno 变成异常 |
| EventLoop | poll(2) | 注册回调，事件触发（把轮询变回调） |
| Parser | 字节流 + 大端 | 循环移位拼出大端整数 |
| TunFD | /dev/net/tun | 一切皆 fd |

## 7. 如何阅读这个仓库

建议顺序：

1. 读根 `CMakeLists.txt` + `etc/*.cmake`，建立"三个变体 + checkN"的构建心智。
2. 读本文件第 4 节数据流，知道收发两条链。
3. 读 `src/byte_stream.hh` → `reassembler.hh` → … 按 checkpoint 顺序，先接口后测试。
4. 读对应 `tests/xxx_test_harness.hh` + `xxx_basics.cc`，这是每个模块的"使用说明书"。

## 8. 与生产级 TCP/IP 的差距（重要认知）

minnow 实现了"正确性主干"（可靠、有序、序号、重组、流量控制、封装、转发），但刻意省略了生产环境的大头：**没有拥塞控制、SACK、快速重传、TCP 选项协商；没有 IP 分片/ICMP；只支持 TCP 和 Ethernet。** 所以它是"能互通的协议栈骨架"，不是生产实现。


## 9. 模块详解

每个模块的实现细节单独成文，放在 `docs/` 目录，避免架构文档臃肿：

- [Reassembler](docs/reassembler.md) —— 乱序重组（check1）
- [WrappingInt32](docs/wrapping_integers.md) —— 序号回绕换算（check2）
- [TCPReceiver](docs/tcp_receiver.md) —— 接收端（check2）

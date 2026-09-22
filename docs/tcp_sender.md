# TCPSender

> 一句话：从应用写入的 ByteStream 中读取字节，在接收窗口允许的范围内生成 TCP 段；保存尚未确认的段，并在超时后重传最早的未确认段。

## 一、核心流程

TCPSender 不自己持续运行。外部在三类事件发生时调用它：

```text
应用有数据 / 窗口更新  → push(transmit)   → 尽量填满接收窗口
收到接收端反馈          → receive(message) → 处理 ACK 和新窗口
时间流逝                → tick(ms, transmit) → 检查是否需要重传
```

发送主线：

```text
应用 writer().push(bytes)
  ↓
出站 ByteStream
  ↓ TCPSender::push()
计算窗口剩余空间
  ↓
按最多 MAX_PAYLOAD_SIZE 读取字节
  ↓
附加 seqno / SYN / FIN / RST，形成 TCPSenderMessage
  ↓
调用 transmit(message) 交给下游
  ↓
保存到 outstanding_，等待 ACK
```

可靠性的核心是两份记账：

```text
next_seqno_   = 下一个新段将使用的绝对序号
acked_seqno_  = 接收方累计确认到的绝对序号

sequence_numbers_in_flight = next_seqno_ - acked_seqno_
```

SYN、每个 payload 字节、FIN 都各占一个序号。

## 二、内部状态

```cpp
ByteStream input_;                  // 应用写入的出站字节流
Wrap32 isn_;                        // 初始序号

uint64_t next_seqno_ {};            // 下一个新序号
uint64_t acked_seqno_ {};           // 已累计确认的位置
uint16_t receiver_window_size_ {1}; // 接收方公布的窗口

bool syn_sent_ {};
bool fin_sent_ {};

deque<OutstandingMessage> outstanding_; // 已发出、尚未完全确认的段

uint64_t timer_elapsed_ms_ {};
uint64_t current_RTO_ms_;
uint64_t consecutive_retransmissions_ {};
```

`OutstandingMessage` 保存原始消息及它结束后的绝对序号，便于根据累计 ACK 删除已确认段。

## 三、接口如何协作

### `push(transmit)`：生成并发送新段

1. 把接收方的零窗口临时当作 1，允许发送一个窗口探测字节。
2. 计算 `window - sequence_numbers_in_flight()`，得到还能占用多少序号。
3. 第一次发送时附加 SYN。
4. 从 `input_.reader()` 读取 payload，长度不超过窗口剩余空间和 `MAX_PAYLOAD_SIZE`。
5. 如果应用已经关闭流、数据也读空，而且窗口还有一个位置，就附加 FIN。
6. 调用外部传入的 `transmit(message)`。
7. 推进 `next_seqno_`，把消息放入 `outstanding_`；若此前没有未确认段，则启动计时器。

### `receive(msg)`：处理 ACK 和窗口

1. RST：给出站 ByteStream 设置错误状态。
2. 更新接收方公布的窗口。
3. 没有 ACK：只保留窗口更新。
4. 用 `next_seqno_` 作 checkpoint，将 32 位 ACK unwrap 为绝对 ACK。
5. 忽略超过 `next_seqno_` 的不可能 ACK，以及没有推进进度的旧/重复 ACK。
6. 推进 `acked_seqno_`，删除所有被完整累计确认的 outstanding 段。
7. 只要 ACK 有新进展，就把 RTO、计时器和连续重传次数恢复到初始状态。

### `tick(ms, transmit)`：超时重传

1. 没有未确认段：不计时。
2. 累计经过时间；未达到当前 RTO 时不做事。
3. 超时后重传 `outstanding_.front()`，即序号最小的未确认段。
4. 普通窗口下：连续重传次数加一，RTO 翻倍（指数退避）。
5. 零窗口探测时：重传，但不增加次数、不退避 RTO。

### `make_empty_message()`

生成不占序号的控制消息：序号是 `wrap(next_seqno_, isn_)`；若出站流出错，则设置 RST。

## 四、模块边界

```text
上游应用
  └─ writer().push()/close() → ByteStream

TCPSender
  ├─ 从 ByteStream.reader() 读取字节
  ├─ 从 TCPReceiverMessage 获取 ACK / window / RST
  └─ 通过 transmit 回调输出 TCPSenderMessage

下游 TCPPeer / 网络适配层
  └─ 接收 TCPSenderMessage，继续封装并发送
```

TCPSender 负责分段、序号、发送窗口、未确认队列和超时重传；它不负责接收重组，也不直接操作 IP 或 Ethernet。

## 五、关键规则

- 初始窗口按 1 处理，因此第一次 `push()` 至少能发 SYN。
- 单个 payload 不超过 `TCPConfig::MAX_PAYLOAD_SIZE`。
- FIN 必须占用窗口中的一个序号；有空间时可与最后一段 payload 合并发送。
- ACK 是累计确认；只有 ACK 真正前进时才重置 RTO 和重传次数。
- 重传不改变 `next_seqno_`，因为它不是新数据。

# TCPReceiver

> 一句话：TCP 接收端。把"线上的 32 位序号 + SYN/FIN"翻译成"绝对字节索引"喂给 Reassembler，并回 ACK + 窗口。

## 一、核心理解链条

```text
TCP段(seqno, SYN, payload, FIN)
  │ receive：
  ├─ SYN → 记录 ISN（初始序号）
  ├─ unwrap(seqno) → 64 位绝对序号
  ├─ 减掉 SYN 占的 1 个序号 → 字节索引
  └─ insert(索引, payload, FIN) → 交给 Reassembler
  │ send：
  ├─ ackno  = bytes_pushed + 1(SYN) + (已关闭? 1(FIN) : 0)
  └─ window = 容量 - 已缓冲
```

它是"协议层"（序号、SYN/FIN）和"字节层"（Reassembler）之间的桥：上层丢给它 TCP 段，它翻译成"绝对索引 + payload + is_last"，交给 check1。

## 二、状态

```cpp
Reassembler reassembler_;          // check1 的乱序重组器
std::optional<Wrap32> isn_ {};     // SYN 的序号，收到 SYN 才设置
```

## 三、接口

```cpp
void receive( TCPSenderMessage message );   // 处理一个到达的段
TCPReceiverMessage send() const;            // 生成 ACK 回执
```

## 四、实现

### receive：翻译并下传

```cpp
if ( message.RST ) { reassembler_.reader().set_error(); return; }
if ( message.SYN ) { isn_ = message.seqno; }
if ( !isn_ ) return;                              // 没 SYN 前忽略一切

const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + 1;  // +1 for SYN
const uint64_t abs_seqno = message.seqno.unwrap( *isn_, checkpoint );

// SYN 占一个序号：有 SYN 时 seqno 指向 SYN（payload 后移一个），无 SYN 时指向 payload
const uint64_t first_index = message.SYN ? abs_seqno : abs_seqno - 1;

reassembler_.insert( first_index, std::move(message.payload), message.FIN );
```

### send：生成 ACK

```cpp
msg.RST = reassembler_.reader().has_error();
msg.window_size = min<uint16_t>( available_capacity, UINT16_MAX );

if ( isn_ ) {
  uint64_t abs_ackno = bytes_pushed + 1;     // +1 for SYN
  if ( is_closed ) abs_ackno += 1;           // +1 for FIN（FIN 已被完整重组）
  msg.ackno = Wrap32::wrap( abs_ackno, *isn_ );
}
```

## 五、和 check0/check1 的关系

```text
TCP段 → TCPReceiver（check2：翻译序号）→ Reassembler（check1：乱序重组）→ ByteStream（check0：缓冲）
```

check2 是接收链的"头"，负责协议层；它把干净的"绝对索引 + payload + is_last"交给 check1。

# WrappingInt32 (Wrap32)

> 一句话：在"32 位会回绕的 TCP 序号"和"64 位绝对序号"之间做换算。

## 一、核心理解

线上 TCP 头里的序号字段只有 32 位，加到 2^32 就回绕到 0；但 Reassembler 需要 64 位、永不回绕的绝对索引。Wrap32 是这两者之间的翻译器：

- **wrap(n, zero_point)**：64 位 → 32 位。取 `zero_point + n` 的低 32 位（uint32 加法自然溢出即取模 2^32）。
- **unwrap(zero_point, checkpoint)**：32 位 → 64 位。同一个 32 位值对应无数个 64 位值（相差 2^32 的倍数），所以拿 `checkpoint` 当锚，返回**离它最近**的那个。

```text
64位绝对序号 ──wrap──> 32位序号（上线传输）
32位序号 ──unwrap(checkpoint)──> 64位绝对序号（内部处理）
```

## 二、接口

```cpp
static Wrap32 wrap( uint64_t n, Wrap32 zero_point );       // 64 → 32
uint64_t unwrap( Wrap32 zero_point, uint64_t checkpoint ) const;  // 32 → 64
```

## 三、实现

### wrap

```cpp
return Wrap32 { zero_point.raw_value_ + static_cast<uint32_t>( n ) };
```

`uint32_t` 加法自然溢出，恰好就是 `(zero_point + n) mod 2^32`。

### unwrap

```cpp
const uint64_t abs = static_cast<uint64_t>( raw_value_ - zero_point.raw_value_ );  // 低32位，[0, 2^32)
uint64_t n = ( checkpoint & ~( (1UL<<32) - 1 ) ) | abs;   // 用 checkpoint 的高位 + abs 的低位

if ( n < checkpoint && checkpoint - n > (1UL<<31) ) {
  n += 1UL<<32;                       // 另一边更近
} else if ( n > checkpoint && n - checkpoint > (1UL<<31) && n >= 1UL<<32 ) {
  n -= 1UL<<32;
}
```

核心：先拼出"与 checkpoint 同位块"的候选，若离 checkpoint 超过半个回绕（2^31），就取相邻一圈的候选。

## 四、关键点

- `unwrap` 的 `n >= 2^32` 守卫：防止减到负数（uint64 下溢成巨大数）。
- 距离恰好等于 2^31 时（tie），保留当前候选，测试按此约定。

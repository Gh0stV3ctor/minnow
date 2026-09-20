# Reassembler

> 一句话：把"带绝对索引、乱序到达的字节碎片"拼成"连续有序的字节流"。
> 本模块最关键的是下面这条理解链条，接口与实现细节在后。

## 一、核心理解

Reassembler 的全部逻辑，浓缩成一个状态：**`next_index_`（下一个该输出的字节位置，初始 0）**。

外部每收到一段数据，就调一次 `insert`。`insert` 只走三步：

1. **裁剪** —— 丢掉"已经输出过的"和"超出容量的"。
2. **暂存** —— 放进按索引排序的 `std::map`，顺便和已有片段合并去重。
3. **输出** —— 凡是能接上 `next_index_` 的，连锁 push 给下游，推进写指针。

```text
外部（事件循环：每收到一段数据调一次）
   │  insert(first_index, data, is_last)
   ▼
① 裁剪：去已写 / 去超容量
   │
② 暂存：merge_into_pending → 放入有序 map（去重叠）
   │
③ 输出：flush_pending → 没洞就 push，推进 next_index_
   │
   ▼
ByteStream（连续、可读的字节流）
```

**有洞就先存，洞补上就连锁输出。循环在外部，模块本身只被调用、不循环、不等待。**

## 二、对外接口

```cpp
explicit Reassembler( ByteStream&& output );     // 构造：指定输出字节流
void insert( uint64_t first_index, std::string data, bool is_last_substring );  // 喂一段碎片
uint64_t bytes_pending() const;                  // 内部暂存了多少字节
Reader& reader();                                // 读输出
const Writer& writer() const;                    // 只读写端
```

## 三、内部状态

```cpp
ByteStream output_;                          // 下游：连续字节写这里
uint64_t next_index_ { 0 };                  // 写指针：下一个要输出的索引
bool eof_ { false };                         // 是否已知流结束
uint64_t eof_index_ { 0 };                   // 流总长度
std::map<uint64_t, std::string> pending_ {}; // 暂存乱序片段（有序、互不重叠）
```

## 四、实现细节

### insert：唯一的入口，编排三步

```cpp
void insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( is_last_substring ) { eof_ = true; eof_index_ = first_index + data.size(); }  // 记录终点

  // ① 裁剪：丢弃已写字节（重复/重叠的前缀）
  if ( first_index < next_index_ ) { ... 切掉前缀；整段写过则 return ... }

  // ① 裁剪：丢弃超容量字节（first_unacceptable = next_index_ + available_capacity）
  if ( ... ) { ... 截断或 return ... }

  // ② 暂存 + ③ 输出
  merge_into_pending( first_index, std::move( data ) );
  flush_pending();

  // 写完后若到达终点，关闭下游
  if ( eof_ && next_index_ >= eof_index_ ) output_.writer().close();
}
```

### merge_into_pending：把片段存进 map，去重叠

维护不变式：`pending_` 里的片段**互不重叠、按索引有序**。插入新片段时：

- 向后合并：若与前面片段重叠/相邻，拼接成一个大片段；若被完全包含则直接丢弃。
- 向前合并：吃掉后面所有重叠/相邻的片段。

```cpp
// 向后合并
auto it = pending_.lower_bound( begin );
if ( it != pending_.begin() ) {
  auto prev = std::prev( it );
  const uint64_t prev_end = prev->first + prev->second.size();
  if ( prev_end >= begin ) {
    if ( prev_end >= end ) return;                              // 完全被包含 → 去重
    data = prev->second + data.substr( prev_end - begin );      // 拼接
    begin = prev->first;
    pending_.erase( prev );
  }
}
// 向前合并
it = pending_.lower_bound( begin );
while ( it != pending_.end() && it->first <= end ) { ... 吃掉并拼接 ... }
pending_[begin] = std::move( data );
```

### flush_pending：把连续的搬给下游

map 有序，`begin()` 永远是最小索引的片段。只要它能接上 `next_index_`，就 push；一旦有洞（最小索引 > next_index_），后面更大的更接不上，直接停。

```cpp
while ( !pending_.empty() ) {
  auto it = pending_.begin();
  if ( it->first > next_index_ ) break;      // 有洞，停
  ... 切掉已写前缀 ...
  output_.writer().push( data );             // 连续 → 写出去
  next_index_ += data.size();                // 写指针前进
  pending_.erase( it );
}
```

## 五、关键设计点

- 用 `std::map` 不用数组：乱序片段稀疏随机，map 按索引有序、天然稀疏。
- 容量窗口 `first_unacceptable = next_index_ + available_capacity()`：等价于 `bytes_popped + capacity`，超出即丢弃。
- merge 必须去重叠：否则 bytes_pending 重复计数、flush 重复输出。
- 提前 return 前都检查 EOF 关闭：防止漏 close。

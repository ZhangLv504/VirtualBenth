#ifndef VIRTUALBENTH_OUTBOUND_QUEUE_H
#define VIRTUALBENTH_OUTBOUND_QUEUE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

// 应用层发送队列的默认逻辑容量上限。它统计仍持有的编码帧字节数，
// 不包含 deque/vector 元数据和 vector 的预留容量；用于防止客户端读取过慢时，
// 连续 DATA 在进程内无限堆积。
inline constexpr std::size_t kDefaultOutboundQueueCapacity = 64U * 1024U;

// 已编码帧在本地队列中的调度类别。
// 该类别不写入线上数据，只决定可否丢弃和控制响应能否越过尚未发送的 DATA。
enum class OutboundFrameKind
{
    Control, // ACK、STATUS、ERROR：不可丢弃，并优先于尚未开始的 DATA
    Data     // DATA：未开始发送时可被控制帧越过，也可因背压被丢弃
};

// 指向当前应发送帧中尚未发送的连续字节。
// 该视图只供调用方立即传给 send()；队列发生修改后不得继续持有。
// OutboundChunk 不拥有内存，真正的字节仍由 OutboundQueue 内部 vector 持有。
struct OutboundChunk
{
    const std::uint8_t* data{nullptr}; // 借用的剩余区间起点，不转移字节所有权
    std::size_t size{0};               // 从 data 开始到当前帧末尾的连续字节数
    OutboundFrameKind kind{OutboundFrameKind::Control}; // 当前帧的调度类别
};

// 保存已经编码完成的协议帧，不依赖 Socket 或平台 API。
// 容量按仍由队列持有的完整 vector.size() 计算，因此部分发送不会提前释放
// 逻辑容量；该数值不是进程堆内存的精确统计。
//
// 核心不变量：
// 1. queued_bytes_ 始终等于两个 deque 中所有完整 vector 的 size() 之和；
// 2. 每个 Entry 的 offset 不大于 bytes.size()；
// 3. 已部分发送的 DATA 必须先补齐，任何控制帧都不能插入一帧的中间；
// 4. 没有部分发送 DATA 时，控制帧优先于尚未开始的 DATA。
class OutboundQueue final
{
public:
    // capacity_bytes 必须大于 0，否则抛出 std::invalid_argument。
    explicit OutboundQueue(
        std::size_t capacity_bytes = kDefaultOutboundQueueCapacity);

    // 参数按值接收，使调用方可以移动交付编码结果，入队时无需再次复制字节。
    // 容量足够时直接排队；不足时先清除尚未开始发送的 DATA，再尝试排队。
    // 若控制帧自身超过容量，或清理后仍被既有控制帧/部分 DATA 占满，则返回 false。
    // 空帧不具备发送语义，会抛出 std::invalid_argument。
    [[nodiscard]] bool enqueue_control(std::vector<std::uint8_t> frame);

    // DATA 不会驱逐任何现有帧；容量不足时保持队列不变并返回 false。
    // 空帧会抛出 std::invalid_argument。
    [[nodiscard]] bool enqueue_data(std::vector<std::uint8_t> frame);

    // 若 DATA 已部分发送，必须先返回该 DATA 的剩余字节；否则控制帧优先。
    // 空队列返回 std::nullopt。队列修改后，该非拥有视图可能失效或不再代表
    // 当前最高优先级发送项，调用方必须重新获取。
    [[nodiscard]] std::optional<OutboundChunk> front() const noexcept;

    // 提交本次实际发送的字节数。完整帧只有在全部字节都提交后才出队。
    // 空队列调用会抛出 std::logic_error；超过当前帧剩余长度会抛出
    // std::out_of_range。允许提交 0B，但不会改变队列状态。
    void consume(std::size_t byte_count);

    // 丢弃所有尚未开始发送的 DATA；已部分发送的队首 DATA 必须保留。
    // 返回实际丢弃的编码字节数。
    std::size_t discard_unsent_data() noexcept;

    // 队列中是否没有任何控制帧或 DATA 帧。
    [[nodiscard]] bool empty() const noexcept;

    // 返回仍由队列持有的完整 vector 字节总数；已发送前缀在整帧出队前仍计入。
    [[nodiscard]] std::size_t queued_bytes() const noexcept;

    // 返回控制队列与 DATA 队列中的完整帧条目总数。
    [[nodiscard]] std::size_t queued_frames() const noexcept;

    // 返回构造时确定的逻辑容量上限。
    [[nodiscard]] std::size_t capacity_bytes() const noexcept;

private:
    // 每个 Entry 对应一帧完整的编码结果。offset 仅记录发送进度，
    // 在整帧完成前 bytes 不缩容，从而保持 front() 返回区间连续。
    struct Entry
    {
        std::vector<std::uint8_t> bytes; // 一帧完整编码字节，在出队前保持存储稳定
        std::size_t offset{0};           // 已被 send() 接受的前缀长度
    };

    // 使用“待加入字节数 <= 剩余容量”判断，避免加法形式发生 size_t 溢出。
    [[nodiscard]] bool can_fit(std::size_t byte_count) const noexcept;

    // current_entry()、current_kind() 与 pop_current() 必须使用完全相同的优先级：
    // 部分 DATA > 控制帧 > 未开始 DATA。否则可能发送一个 Entry 却消费另一个。
    [[nodiscard]] Entry* current_entry() noexcept;
    [[nodiscard]] const Entry* current_entry() const noexcept;
    [[nodiscard]] OutboundFrameKind current_kind() const noexcept;
    void pop_current();

    std::size_t capacity_bytes_;       // 构造后固定的最大持有字节数
    std::size_t queued_bytes_{0};      // 两类队列中完整 vector 的总字节数
    std::deque<Entry> control_frames_; // 控制帧内部保持 FIFO
    std::deque<Entry> data_frames_;    // DATA 帧内部保持 FIFO
};

#endif // VIRTUALBENTH_OUTBOUND_QUEUE_H

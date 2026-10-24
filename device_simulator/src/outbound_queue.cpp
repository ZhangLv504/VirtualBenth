#include "device_simulator/outbound_queue.h"

#include <iterator>
#include <stdexcept>
#include <utility>

namespace
{
    // 零长度条目既不是合法协议帧，也会让网络层出现“有队列元素但没有字节可发”
    // 的矛盾状态，因此在两种入队入口统一拒绝。
    void validate_frame(const std::vector<std::uint8_t>& frame)
    {
        if (frame.empty())
        {
            throw std::invalid_argument("outbound frame must not be empty");
        }
    }
}

OutboundQueue::OutboundQueue(std::size_t capacity_bytes)
    : capacity_bytes_(capacity_bytes)
{
    // 0 容量无法容纳任何合法帧，与队列的用途矛盾；在构造阶段立即失败，
    // 避免后续每个入队分支都维护一个无意义的特殊状态。
    if (capacity_bytes_ == 0)
    {
        throw std::invalid_argument("outbound queue capacity must be greater than zero");
    }
}

bool OutboundQueue::enqueue_control(std::vector<std::uint8_t> frame)
{
    validate_frame(frame);

    // 单帧已经超过硬上限时，即使清空全部可丢弃 DATA 也不可能成功。
    if (frame.size() > capacity_bytes_)
    {
        return false;
    }

    // 控制响应不能长期排在尚未发送的 DATA 后面。容量不足时，先回收所有
    // 可以安全丢弃的 DATA；已部分发送的 DATA 会由 discard_unsent_data() 保留。
    if (!can_fit(frame.size()))
    {
        discard_unsent_data();
    }

    // 清理后仍可能被已排队控制帧或部分发送 DATA 占满，此时不能突破硬上限。
    if (!can_fit(frame.size()))
    {
        return false;
    }

    // 在 move 前读取 size 并更新容量账目；move 后 frame 处于有效但内容未指定状态。
    queued_bytes_ += frame.size();
    control_frames_.push_back(Entry{std::move(frame), 0});
    return true;
}

bool OutboundQueue::enqueue_data(std::vector<std::uint8_t> frame)
{
    validate_frame(frame);

    // DATA 属于可施加背压的数据流，不允许为了新 DATA 驱逐旧帧，也不允许扩容。
    // 因此失败路径在修改任何成员前返回，保证队列无副作用。
    if (!can_fit(frame.size()))
    {
        return false;
    }

    queued_bytes_ += frame.size();
    data_frames_.push_back(Entry{std::move(frame), 0});
    return true;
}

std::optional<OutboundChunk> OutboundQueue::front() const noexcept
{
    const Entry* entry = current_entry();
    if (entry == nullptr)
    {
        return std::nullopt;
    }

    // offset 之前的字节已经由 send() 接受；只把尚未发送的连续尾部暴露给调用方。
    // 返回的是借用指针，consume()/入队/丢弃等修改操作后调用方必须重新获取 front()。
    return OutboundChunk{
        entry->bytes.data() + entry->offset,
        entry->bytes.size() - entry->offset,
        current_kind()};
}

void OutboundQueue::consume(std::size_t byte_count)
{
    Entry* entry = current_entry();
    if (entry == nullptr)
    {
        throw std::logic_error("cannot consume an empty outbound queue");
    }

    const std::size_t remaining = entry->bytes.size() - entry->offset;
    // 网络层只能提交 send() 实际返回的字节数；越过当前帧会破坏下一帧边界。
    if (byte_count > remaining)
    {
        throw std::out_of_range("consumed bytes exceed the current frame");
    }

    entry->offset += byte_count;
    if (entry->offset == entry->bytes.size())
    {
        // 只有完整帧发送结束才释放 vector 和容量。部分发送只推进 offset，
        // queued_bytes_ 仍按完整持有内存计费。
        pop_current();
    }
}

std::size_t OutboundQueue::discard_unsent_data() noexcept
{
    std::size_t dropped_bytes = 0;

    // TCP 字节流中已发送的帧前缀不能撤回。若队首 DATA 的 offset 非零，
    // 它必须保留并在后续可写事件中补齐；清理从它的下一帧开始。
    const bool preserve_front =
        !data_frames_.empty() && data_frames_.front().offset != 0;
    auto first_dropped = preserve_front
        ? std::next(data_frames_.begin())
        : data_frames_.begin();

    // queued_bytes_ 按完整 vector 计费，所以丢弃时同样减去每个完整帧的大小。
    for (auto it = first_dropped; it != data_frames_.end(); ++it)
    {
        dropped_bytes += it->bytes.size();
    }
    data_frames_.erase(first_dropped, data_frames_.end());
    queued_bytes_ -= dropped_bytes;
    return dropped_bytes;
}

bool OutboundQueue::empty() const noexcept
{
    // 两个物理队列共同构成一个逻辑发送队列，任一非空都表示仍有待处理帧。
    return control_frames_.empty() && data_frames_.empty();
}

std::size_t OutboundQueue::queued_bytes() const noexcept
{
    return queued_bytes_;
}

std::size_t OutboundQueue::queued_frames() const noexcept
{
    return control_frames_.size() + data_frames_.size();
}

std::size_t OutboundQueue::capacity_bytes() const noexcept
{
    return capacity_bytes_;
}

bool OutboundQueue::can_fit(std::size_t byte_count) const noexcept
{
    // queued_bytes_ 始终不超过 capacity_bytes_，因此减法不会下溢；
    // 与 queued_bytes_ + byte_count <= capacity_bytes_ 相比，此写法还避免加法溢出。
    return byte_count <= capacity_bytes_ - queued_bytes_;
}

OutboundQueue::Entry* OutboundQueue::current_entry() noexcept
{
    // 最高优先级：DATA 一旦已有前缀进入 TCP 流，就必须发送到帧尾，
    // 否则插入控制帧会令接收端把两帧字节拼成一个损坏帧。
    if (!data_frames_.empty() && data_frames_.front().offset != 0)
    {
        return &data_frames_.front();
    }

    // 没有不可打断的 DATA 时，让控制响应越过所有尚未开始的 DATA。
    if (!control_frames_.empty())
    {
        return &control_frames_.front();
    }

    // 最后才按 FIFO 发送普通 DATA。
    if (!data_frames_.empty())
    {
        return &data_frames_.front();
    }
    return nullptr;
}

const OutboundQueue::Entry* OutboundQueue::current_entry() const noexcept
{
    // const 重载与非 const 重载必须保持相同选择规则，保证 front() 观察到的帧
    // 与 consume() 随后推进的帧完全一致。
    if (!data_frames_.empty() && data_frames_.front().offset != 0)
    {
        return &data_frames_.front();
    }
    if (!control_frames_.empty())
    {
        return &control_frames_.front();
    }
    if (!data_frames_.empty())
    {
        return &data_frames_.front();
    }
    return nullptr;
}

OutboundFrameKind OutboundQueue::current_kind() const noexcept
{
    // 选择顺序必须与 current_entry() 同步；该函数只为 OutboundChunk 标注类别。
    if (!data_frames_.empty() && data_frames_.front().offset != 0)
    {
        return OutboundFrameKind::Data;
    }
    if (!control_frames_.empty())
    {
        return OutboundFrameKind::Control;
    }
    return OutboundFrameKind::Data;
}

void OutboundQueue::pop_current()
{
    // 删除顺序必须与 current_entry() 完全一致。每个分支都先按完整 vector 大小
    // 归还容量，再删除 Entry，维持 queued_bytes_ 的总和不变量。
    if (!data_frames_.empty() && data_frames_.front().offset != 0)
    {
        queued_bytes_ -= data_frames_.front().bytes.size();
        data_frames_.pop_front();
        return;
    }
    if (!control_frames_.empty())
    {
        queued_bytes_ -= control_frames_.front().bytes.size();
        control_frames_.pop_front();
        return;
    }

    // 能进入此分支说明控制队列为空且至少存在一帧未开始的 DATA。
    queued_bytes_ -= data_frames_.front().bytes.size();
    data_frames_.pop_front();
}

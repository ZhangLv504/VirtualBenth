#ifndef VIRTUALBENTH_DATA_FRAME_BUILDER_H
#define VIRTUALBENTH_DATA_FRAME_BUILDER_H

#include "device_simulator/sampling_context.h"
#include "protocol/frame.h"
#include "device_simulator/outbound_queue.h"

#include <cstdint>
#include <vector>

#include "utc_sample_timeline.h"

// 依据显式输入构造单通道 DATA 业务帧。
//
// 本函数只负责 DATA payload 的字段顺序和小端序列化，不读取系统时钟、不维护
// 序号、不生成波形，也不操作发送队列。samples 的元素个数直接写入 cnt；超过
// 协议单帧上限 1020 时抛出 std::invalid_argument，调用方不会得到部分 Frame。
[[nodiscard]] Frame make_data_frame(
    std::uint8_t channel_id,
    std::uint32_t sequence,
    std::uint64_t timestamp_us,
    const std::vector<float>& samples);

// 为一个通道把一批样本组成单帧 DATA 并完整入队。
//
// 只有 enqueue_data() 返回 true 才提交该通道序号；返回 false 时队列、
// 序号和入参都不变。sample_count 超过协议单帧上限时 make_data_frame()
// 抛异常，此时序号同样未提交。
//
// channel_id 的前置条件是 0～7，必须由调用方遍历 channel_mask 的 bit0～bit7
// 产生。本函数不做范围校验：std::uint8_t 允许 8～255，而序号数组固定为 8 个
// 元素，越界的 current_sequence() 读到脏值、commit_sequence() 会改写对象外的
// 内存；RelWithDebInfo 定义了 NDEBUG，MSVC 也不会给出迭代器调试断言。
[[nodiscard]] bool enqueue_channel_data(
    SamplingContext& sampling_context,
    OutboundQueue& outbound_queue,
    std::uint8_t channel_id,
    std::uint64_t timestamp_us,
    const std::vector<float>& samples);


// 把一个分段按 channel_mask 依次入队为所有启用通道各一帧 DATA，并在全部
// 通道成功后把 UTC 时间线推进该分段一次。
//
// 时间戳取自 timeline.current_ts_us()，所有通道共享同一 ts 和 cnt；各通道
// 使用自己的序号。按 bit0～bit7 即通道 0～7 升序遍历。
//
// 任一通道 enqueue_channel_data() 返回 false 时立即返回 false，不重试、
// 不回滚、不推进时间线；后续清队列、ERROR(INTERNAL) 和关闭连接由调用方处理。
//
// 前置条件：sampling_context.sample_rate 必须等于 timeline.sample_rate()。
// 两者分别由 START 写入，不一致时帧内 cnt 与时间线推进速率会互相矛盾。
//
// channel_mask 为 0 时循环体不执行，时间线按 0 个样本推进（起点和余数均不变）
// 并返回 true（空真）；START 已拒绝掩码 0，因此运行时不会走到，测试须单独覆盖。
// samples.size() 超过 1020 时在任何副作用发生前抛 std::invalid_argument：
// 无帧入队、无序号提交、时间线不变。
[[nodiscard]] bool enqueue_segment_channels(
    SamplingContext& sampling_context,
    OutboundQueue& outbound_queue,
    UtcSampleTimeline& timeline,
    const std::vector<float>& samples);

#endif // VIRTUALBENTH_DATA_FRAME_BUILDER_H

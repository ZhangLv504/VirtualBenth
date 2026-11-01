#ifndef VIRTUALBENTH_DATA_FRAME_BUILDER_H
#define VIRTUALBENTH_DATA_FRAME_BUILDER_H

#include "device_simulator/sampling_context.h"
#include "protocol/frame.h"
#include "device_simulator/outbound_queue.h"

#include <cstdint>
#include <vector>

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
    const std::vector<float>& samples
);

#endif // VIRTUALBENTH_DATA_FRAME_BUILDER_H

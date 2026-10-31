#ifndef VIRTUALBENTH_DATA_FRAME_BUILDER_H
#define VIRTUALBENTH_DATA_FRAME_BUILDER_H

#include "device_simulator/data_frame_limits.h"
#include "protocol/frame.h"

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

#endif // VIRTUALBENTH_DATA_FRAME_BUILDER_H

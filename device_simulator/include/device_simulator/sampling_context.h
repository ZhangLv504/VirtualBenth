#ifndef VIRTUALBENTH_SAMPLING_CONTEXT_H
#define VIRTUALBENTH_SAMPLING_CONTEXT_H

#include <array>
#include <cstdint>

// 当前 TCP 连接内一次采集会话的可变上下文。
//
// START 的 ACK+STATUS 全部发送后整体初始化，STOP 的响应全部发送后清空；
// 连接结束时随 TcpServer::run() 的局部作用域销毁，不会泄漏到下一条连接。
//
// DATA 序号只能经 current_sequence() 读、经 commit_sequence() 递增：先读取序号
// 组帧，编码和入队都成功后才提交递增，任一环节失败都不占用该序号。两个方法的
// channel_id 前置条件为 0～7，由调用方从 channel_mask 的 bit0～bit7 循环产生。
class SamplingContext final
{
public:
    // 原子地应用新 START 配置，并把所有通道序号恢复到新会话起点 0。
    void start(std::uint32_t new_sample_rate, std::uint8_t new_channel_mask) noexcept
    {
        sample_rate = new_sample_rate;
        channel_mask = new_channel_mask;
        sequence_by_channel.fill(0);
    }

    // 清除停止会话后的全部配置和序号，使旧采集数据不能污染下次 START。
    void reset() noexcept
    {
        sample_rate = 0;
        channel_mask = 0;
        sequence_by_channel.fill(0);
    }

    // 只读返回该通道下一帧应写入的 seq，不修改任何状态。
    [[nodiscard]] std::uint32_t current_sequence(std::uint32_t channel_id) const noexcept
    {
        return sequence_by_channel[channel_id];
    }

    // 该帧已完整入队后调用，占用一个序号；uint32_t 自然回绕，禁止加边界判断。
    void commit_sequence(std::uint32_t channel_id) noexcept
    {
        ++sequence_by_channel[channel_id];
    }

    std::uint32_t sample_rate{0}; // START 指定的每通道每秒样本数；0 表示未配置
    std::uint8_t channel_mask{0}; // bit0～bit7 分别表示通道 0～7 是否启用

private:
    std::array<std::uint32_t, 8> sequence_by_channel{}; // 各通道下一 DATA 序号
};

#endif //VIRTUALBENTH_SAMPLING_CONTEXT_H
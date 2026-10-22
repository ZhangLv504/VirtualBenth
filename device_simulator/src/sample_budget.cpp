#include "device_simulator/sample_budget.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace
{
    // 校验采样率：协议和业务逻辑均不允许采样率为 0
    void validate_sample_rate(std::uint32_t sample_rate)
    {
        if (sample_rate == 0)
        {
            throw std::invalid_argument("sample_rate must be greater than zero");
        }
    }
}

SampleBudget::SampleBudget(std::uint32_t sample_rate)
    : sample_rate_(sample_rate)
{
    validate_sample_rate(sample_rate_);
}

TickSampleBudget SampleBudget::next_tick()
{
    // 核心：无浮点误差的积分累加算法
    // credit_ 在取模后必然小于 50。每次 tick 累加一次 sample_rate_，
    // 相当于把样本切分成 50 份碎片。凑够 50 个碎片就兑换 1 个完整样本，
    // 从而彻底避免浮点数漂移，保证长程总样本数绝对精确。
    credit_ += sample_rate_;
    const std::uint64_t total = credit_ / kDataTicksPerSecond;
    // 保留余数（碎片）到下一个 tick 继续累加
    credit_ %= kDataTicksPerSecond;

    TickSampleBudget result;
    result.total_samples = static_cast<std::uint32_t>(total);

    // 预计算需要拆分的帧数，向上取整 (total / 1020)，预留 vector 容量避免多次扩容。
    // 注：修正了代码中的减号 (total + max - 1) / max 才是正确的向上取整写法。
    const std::uint64_t segment_count =
        (total + kMaxSamplesPerDataFrame - 1) / kMaxSamplesPerDataFrame;
    result.segments.reserve(static_cast<std::size_t>(segment_count));

    // 拆帧逻辑：将 total 拆分成若干个不超过 1020 的片段
    std::uint64_t remaining = total;
    while (remaining != 0)
    {
        // 本次分段取 remaining 和单帧上限 1020 之间的较小值
        const auto segment = static_cast<std::uint16_t>(
            std::min<std::uint64_t>(remaining, kMaxSamplesPerDataFrame));
        result.segments.push_back(segment);
        remaining -= segment;
    }

    return result;
}

void SampleBudget::reset(std::uint32_t sample_rate)
{
    validate_sample_rate(sample_rate);
    sample_rate_ = sample_rate;
    // 重置会话时，必须清零积分池，防止上一轮的进度污染新一轮的采样起始状态
    credit_ = 0;
}

std::uint32_t SampleBudget::sample_rate() const noexcept
{
    return sample_rate_;
}

std::uint64_t SampleBudget::credit() const noexcept
{
    return credit_;
}

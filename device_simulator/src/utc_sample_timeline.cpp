#include "device_simulator/utc_sample_timeline.h"

#include <stdexcept>

namespace
{
    // 集中维护构造和 reset() 的共同前置条件。先校验、后修改对象，确保
    // reset(0, ts) 抛出异常时旧的速率、批次起点和余数仍保持原样。
    void validate_sample_rate(std::uint32_t sample_rate)
    {
        if (sample_rate == 0)
        {
            throw std::invalid_argument("sample_rate must be greater than zero");
        }
    }

    // 一个微秒等于 1e-6 秒，因此 cnt 个样本跨 cnt × 1e6 / sample_rate 微秒。
    constexpr std::uint64_t kMicrosecondsPerSecond = 1000000ULL;
}

UtcSampleTimeline::UtcSampleTimeline(std::uint32_t sample_rate, std::uint64_t start_ts_us)
    : sample_rate_(sample_rate)
    , ts_us_(start_ts_us)
{
    validate_sample_rate(sample_rate);
}

void UtcSampleTimeline::reset(std::uint32_t sample_rate, std::uint64_t start_ts_us)
{
    validate_sample_rate(sample_rate);
    sample_rate_ = sample_rate;
    ts_us_ = start_ts_us;
    // 新会话从零余数开始；沿用上一轮余数会让首个分段的时间轴提前偏移。
    remainder_ = 0;
}

void UtcSampleTimeline::advance(std::uint16_t sample_count)
{
    const std::uint64_t numerator = remainder_ + static_cast<std::uint64_t>(sample_count) * kMicrosecondsPerSecond;
    ts_us_ += numerator / sample_rate_;
    remainder_ = numerator % sample_rate_;
}

std::uint64_t UtcSampleTimeline::current_ts_us() const noexcept
{
    return ts_us_;
}

std::uint64_t UtcSampleTimeline::remainder() const noexcept
{
    return remainder_;
}

std::uint32_t UtcSampleTimeline::sample_rate() const noexcept
{
    return sample_rate_;
}

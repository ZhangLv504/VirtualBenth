#ifndef VIRTUALBENTH_UTC_SAMPLE_TIMELINE_H
#define VIRTUALBENTH_UTC_SAMPLE_TIMELINE_H

#include <cstdint>

class UtcSampleTimeline final
{
public:
    UtcSampleTimeline(std::uint32_t sample_rate, std::uint64_t start_ts_us);

    void reset(std::uint32_t sample_rate, std::uint64_t start_ts_us);

    // 按 sample_count 个样本推进批次起点：ts += (remainder + sample_count × 1e6) / rate，
    // remainder = (remainder + sample_count × 1e6) % rate。
    //
    // 形参 widen 到 uint64 是为了支持一次推进批量丢弃的样本数（若干个 tick 的总和），
    // 单个 DATA 分段仍只传 ≤1020。安全上界：sample_count < 2^64 / 1e6 ≈ 1.8×10^13；
    // 超过它与 rate 为 0（构造与 reset 已排除）都是未处理的前置条件违反。
    void advance(std::uint64_t sample_count);

    [[nodiscard]] std::uint64_t current_ts_us() const noexcept;

    [[nodiscard]] std::uint64_t remainder() const noexcept;

    [[nodiscard]] std::uint32_t sample_rate() const noexcept;

private:
    std::uint32_t sample_rate_;     // 每通道每秒样本数，始终大于 0
    std::uint64_t ts_us_{0};        // 当前分段批次起点，UTC Epoch 微秒
    std::uint64_t remainder_{0};    // 跨分段保留的亚微秒余数，始终小于 sample_rate_
};

#endif //VIRTUALBENTH_UTC_SAMPLE_TIMELINE_H
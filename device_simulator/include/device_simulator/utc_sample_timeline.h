#ifndef VIRTUALBENTH_UTC_SAMPLE_TIMELINE_H
#define VIRTUALBENTH_UTC_SAMPLE_TIMELINE_H

#include <cstdint>

class UtcSampleTimeline final
{
public:
    UtcSampleTimeline(std::uint32_t sample_rate, std::uint64_t start_ts_us);

    void reset(std::uint32_t sample_rate, std::uint64_t start_ts_us);

    void advance(std::uint16_t sample_count);

    [[nodiscard]] std::uint64_t current_ts_us() const noexcept;

    [[nodiscard]] std::uint64_t remainder() const noexcept;

    [[nodiscard]] std::uint32_t sample_rate() const noexcept;

private:
    std::uint32_t sample_rate_;     // 每通道每秒样本数，始终大于 0
    std::uint64_t ts_us_{0};        // 当前分段批次起点，UTC Epoch 微秒
    std::uint64_t remainder_{0};    // 跨分段保留的亚微秒余数，始终小于 sample_rate_
};

#endif //VIRTUALBENTH_UTC_SAMPLE_TIMELINE_H
#ifndef VIRTUALBENTH_SAMPLE_BUDGET_H
#define VIRTUALBENTH_SAMPLE_BUDGET_H

#include <cstdint>
#include <vector>

// 每秒逻辑 tick 数量（固定 20ms 一次 tick，即 1000/20 = 50）
inline constexpr std::uint32_t kDataTicksPerSecond = 50;
// 协议规定单帧最大承载的样本数（受限于 4096B payload 最大长度与 15B 头部开销）[cite: 1]
inline constexpr std::uint16_t kMaxSamplesPerDataFrame = 1020;

// 单次 tick 的样本分配结果
struct TickSampleBudget
{
    std::uint32_t total_samples{0};       // 当前 tick 需要发送的样本总数
    std::vector<std::uint16_t> segments;  // 超出 1020 上限时，拆分后的各帧样本数切片集合
};

// 样本预算分配器（整数累加算法）
// 把每秒 sample_rate 个样本无浮点地分配到 50 个 20ms 逻辑 tick。
// credit 跨 tick 保存不足一个样本的余数；每个分段都可直接放入 DATA.cnt。
class SampleBudget final
{
public:
    // 构造函数，初始化并校验采样率
    explicit SampleBudget(std::uint32_t sample_rate);

    // 推进到下一个 tick，计算并返回本次 tick 应发的样本总数和拆帧切片
    [[nodiscard]] TickSampleBudget next_tick();

    // 新采集会话必须重置 credit，避免上一会话的分数预算泄漏进来。
    void reset(std::uint32_t sample_rate);

    [[nodiscard]] std::uint32_t sample_rate() const noexcept;
    [[nodiscard]] std::uint64_t credit() const noexcept;

private:
    std::uint32_t sample_rate_; // 目标采样率（每秒样本数）
    std::uint64_t credit_{0};   // 样本积分池（不足一个完整样本的碎片累加器）
};

#endif // VIRTUALBENTH_SAMPLE_BUDGET_H

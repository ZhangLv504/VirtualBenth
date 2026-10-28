#ifndef VIRTUALBENTH_SAMPLE_BUDGET_H
#define VIRTUALBENTH_SAMPLE_BUDGET_H

#include <cstdint>
#include <vector>

// 每秒逻辑 tick 数量：当前调度周期固定为 20ms，因此每秒推进 50 次预算。
inline constexpr std::uint32_t kDataTicksPerSecond = 50;
// DATA 固定字段占 15B、每个 int32 样本占 4B；在 4096B payload 上限内最多 1020 个。
inline constexpr std::uint16_t kMaxSamplesPerDataFrame = 1020;

// 一个逻辑 tick 的样本发送预算。
//
// total_samples 表示该 tick 应产生的总样本数；segments 按协议单帧上限把总数拆成
// 多段，每个元素可直接写入一个 DATA 帧的 cnt。total_samples 为 0 时 segments 为空。
struct TickSampleBudget
{
    std::uint32_t total_samples{0};      // 本 tick 应生成的样本总数
    std::vector<std::uint16_t> segments; // 各 DATA 帧样本数，顺序之和等于 total_samples
};

// 使用整数积分法把每秒 sample_rate 个样本分配到 50 个逻辑 tick。
//
// 每次 next_tick() 把 sample_rate 加入 credit_，再用除法兑换本 tick 的完整样本，
// 余数继续跨 tick 保存。这样既能表达低于 50Hz 的采样率，也不会产生浮点累计误差。
// 类实例属于一次采集会话；新 START 应调用 reset()，不能沿用上一会话的余数。
class SampleBudget final
{
public:
    // 创建初始预算器。sample_rate 是每通道每秒样本数，必须大于 0；否则抛出
    // std::invalid_argument。构造后第一 tick 从零 credit 开始。
    explicit SampleBudget(std::uint32_t sample_rate);

    // 推进恰好一个逻辑 tick。先累计积分、兑换完整样本，再按每帧 1020 个上限
    // 拆分 segments；返回后 credit_ 只保留不足一个样本的积分余数。
    [[nodiscard]] TickSampleBudget next_tick();

    // 开始新的采集会话：校验并替换采样率，同时清零积分余数。
    // sample_rate 为 0 时抛出 std::invalid_argument，且对象原状态保持不变。
    void reset(std::uint32_t sample_rate);

    // 返回当前配置的每秒样本数，不推进预算。
    [[nodiscard]] std::uint32_t sample_rate() const noexcept;

    // 返回尚未兑换成完整样本的积分；正常状态下恒小于 kDataTicksPerSecond。
    [[nodiscard]] std::uint64_t credit() const noexcept;

private:
    std::uint32_t sample_rate_; // 当前会话每通道每秒样本数，始终大于 0
    std::uint64_t credit_{0};   // 跨 tick 积分余数，单位为“1/50 个样本”
};

#endif // VIRTUALBENTH_SAMPLE_BUDGET_H

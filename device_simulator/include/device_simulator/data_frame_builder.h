#ifndef VIRTUALBENTH_DATA_FRAME_BUILDER_H
#define VIRTUALBENTH_DATA_FRAME_BUILDER_H

#include "device_simulator/sampling_context.h"
#include "protocol/frame.h"
#include "device_simulator/outbound_queue.h"

#include <cstdint>
#include <vector>

#include "sample_budget.h"
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
// channel_mask 为 0 时不入队、不推进时间线并返回 false；返回值无法与
// 队列容量失败区分，调用方按同一失败语义处理（清未开始 DATA、尽力排
// ERROR(INTERNAL)、关闭连接）。START 按协议 5.2 已拒绝掩码 0，STOP 后
// 上下文 reset 为 0 且运行时不再生成 DATA，因此该分支运行时不可达。
[[nodiscard]] bool enqueue_segment_channels(
    SamplingContext& sampling_context,
    OutboundQueue& outbound_queue,
    UtcSampleTimeline& timeline,
    const std::vector<float>& samples);


// 推进恰好一个逻辑 tick：取一次样本预算，按分段顺序为每个分段生成确定性样本，
// 并整段交给分段级边界 enqueue_segment_channels() 入队。
//
// 处理顺序：sample_budget.next_tick() 得到 segments → 逐段构造大小恰为该分段
// 样本数的 vector（值取段内序号 mod 256，仅用于让帧内容可逐字节断言，不属于
// 波形语义）→ 对该分段的所有启用通道入队 → 下一分段。segments 为空时循环体
// 不执行，直接返回 true。
//
// 所有权与生命周期：四个参数全部为借用引用，本函数不持有、不销毁、也不创建
// 任何状态；它们必须属于同一次成功 START 建立的采集会话。隐含前置条件
// sampling_context.sample_rate == sample_budget.sample_rate() ==
// timeline.sample_rate()，三者不一致时帧内 cnt、预算分配和时间线推进速率互相
// 矛盾，本函数不校验（内部可信调用方）。
//
// 状态提交：时间线只经由 enqueue_segment_channels() 内部推进，本函数绝不调用
// advance()，否则同一分段会被双倍前进。next_tick() 一旦返回，本次积分余数即
// 已消耗且无法退回，因此本函数不提供任何"把预算还给下一 tick"的路径。
//
// 失败语义：任一 enqueue_segment_channels() 返回 false 时立即返回 false，不
// 重试、不回滚已成功入队的前序分段和帧、不处理剩余分段。清理尚未开始发送的
// DATA、尽力排入 ERROR(INTERNAL) 和关闭连接均由调用方负责。按队列溢出策略，
// 失败即意味着关闭连接，本会话不再有需要兑现的后续 tick，故预算不退回不会
// 造成样本错位。
//
// 明确不属于本函数的职责：读取 system_clock 或 steady_clock、计算或推进 20ms
// 截止时间、判断 Running 状态、处理 STOP 优先级、调用 send()、修改 select()
// 事件循环、生成真实波形。
//
// 两种"没有入队"的情况必须区分：sample_rate 低于 50Hz 时部分 tick 的
// total_samples 为 0，属于合法空 tick，返回 true 且时间线不变；
// sampling_context.channel_mask 为 0 时由 enqueue_segment_channels() 返回
// false，属于不可达配置（START 按协议 5.2 已拒绝掩码 0）。
[[nodiscard]] bool run_data_tick(
    SamplingContext& sampling_context,
    OutboundQueue& outbound_queue,
    UtcSampleTimeline& timeline,
    SampleBudget& sample_budget);

#endif // VIRTUALBENTH_DATA_FRAME_BUILDER_H

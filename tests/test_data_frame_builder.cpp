#include "catch_amalgamated.hpp"

#include "device_simulator/data_frame_builder.h"
#include "device_simulator/data_frame_limits.h"
#include "device_simulator/outbound_queue.h"
#include "device_simulator/sample_budget.h"
#include "device_simulator/sampling_context.h"
#include "device_simulator/utc_sample_timeline.h"
#include "protocol/protocol_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

TEST_CASE("DATA frame builder matches the frozen protocol vector",
          "[simulator][data-frame][encoding]")
{
    const Frame frame = make_data_frame(
        0,
        1024,
        UINT64_C(1712345678000),
        std::vector<float>{1.5F, -0.2F, 0.8F});

    const EncodeResult encoded = ProtocolCodec::encode(frame);
    REQUIRE(encoded.ok());
    REQUIRE((encoded.bytes == std::vector<std::uint8_t>{
        0xA5, 0x5A, 0x01, 0x04, 0x1B, 0x00,
        0x00,
        0x00, 0x04, 0x00, 0x00,
        0xB0, 0x80, 0xC1, 0xAF, 0x8E, 0x01, 0x00, 0x00,
        0x03, 0x00,
        0x00, 0x00, 0xC0, 0x3F,
        0xCD, 0xCC, 0x4C, 0xBE,
        0xCD, 0xCC, 0x4C, 0x3F,
        0xD4, 0xC2,
    }));
}

TEST_CASE("DATA frame builder enforces the protocol sample limit",
          "[simulator][data-frame][validation]")
{
    const std::vector<float> maximum_samples(kMaxSamplesPerDataFrame, 1.0F);
    const Frame maximum = make_data_frame(7, 0, 0, maximum_samples);
    REQUIRE(maximum.payload.size() == 4095);
    REQUIRE(maximum.payload[13] == 0xFC);
    REQUIRE(maximum.payload[14] == 0x03);
    REQUIRE(ProtocolCodec::encode(maximum).ok());

    const std::vector<float> too_many_samples(
        static_cast<std::size_t>(kMaxSamplesPerDataFrame) + 1,
        1.0F);
    REQUIRE_THROWS_AS(
        make_data_frame(0, 0, 0, too_many_samples),
        std::invalid_argument);
}

TEST_CASE("channel DATA enqueue commits its sequence only after success",
          "[simulator][data-frame][queue][sequence]")
{
    SamplingContext context;
    context.start(1000, 0x04);
    OutboundQueue queue(1024);
    const std::vector<float> samples{1.5F, -0.2F, 0.8F};
    constexpr std::uint64_t kTimestampUs = 1712345678000ULL;

    const EncodeResult expected = ProtocolCodec::encode(
        make_data_frame(2, 0, kTimestampUs, samples));
    REQUIRE(expected.ok());

    REQUIRE(enqueue_channel_data(context, queue, 2, kTimestampUs, samples));
    REQUIRE(context.current_sequence(2) == 1);
    REQUIRE(context.current_sequence(1) == 0);
    REQUIRE(queue.queued_frames() == 1);

    const auto queued = queue.front();
    REQUIRE(queued.has_value());
    REQUIRE(queued->kind == OutboundFrameKind::Data);
    REQUIRE(queued->size == expected.bytes.size());
    REQUIRE(std::equal(
        expected.bytes.begin(), expected.bytes.end(), queued->data));
}

TEST_CASE("channel DATA enqueue preserves sequence when capacity rejects the frame",
          "[simulator][data-frame][queue][backpressure]")
{
    SamplingContext context;
    context.start(1000, 0x01);
    OutboundQueue queue(8);

    REQUIRE_FALSE(enqueue_channel_data(
        context, queue, 0, 1000, std::vector<float>{1.0F}));
    REQUIRE(context.current_sequence(0) == 0);
    REQUIRE(queue.queued_bytes() == 0);
    REQUIRE(queue.queued_frames() == 0);
    REQUIRE_FALSE(queue.front().has_value());
}

TEST_CASE("channel DATA enqueue preserves sequence when frame construction throws",
          "[simulator][data-frame][queue][validation]")
{
    SamplingContext context;
    context.start(1000, 0x01);
    OutboundQueue queue(64 * 1024);
    const std::vector<float> too_many_samples(
        static_cast<std::size_t>(kMaxSamplesPerDataFrame) + 1,
        1.0F);

    REQUIRE_THROWS_AS(
        enqueue_channel_data(context, queue, 0, 1000, too_many_samples),
        std::invalid_argument);
    REQUIRE(context.current_sequence(0) == 0);
    REQUIRE(queue.queued_bytes() == 0);
    REQUIRE(queue.queued_frames() == 0);
}

TEST_CASE("DATA segment enqueues enabled channels in ascending order with one timestamp",
          "[simulator][data-frame][segment][sequence][timeline]")
{
    constexpr std::uint64_t kTimestampUs = 1712345678000ULL;
    const std::vector<float> samples{1.5F, -0.2F, 0.8F};
    SamplingContext context;
    context.start(1000, 0x05); // 通道 0 和 2
    OutboundQueue queue(1024);
    UtcSampleTimeline timeline(1000, kTimestampUs);

    const EncodeResult expected_channel_0 = ProtocolCodec::encode(
        make_data_frame(0, 0, kTimestampUs, samples));
    const EncodeResult expected_channel_2 = ProtocolCodec::encode(
        make_data_frame(2, 0, kTimestampUs, samples));
    REQUIRE(expected_channel_0.ok());
    REQUIRE(expected_channel_2.ok());

    REQUIRE(enqueue_segment_channels(context, queue, timeline, samples));
    REQUIRE(context.current_sequence(0) == 1);
    REQUIRE(context.current_sequence(1) == 0);
    REQUIRE(context.current_sequence(2) == 1);
    REQUIRE(queue.queued_frames() == 2);
    REQUIRE(timeline.current_ts_us() == kTimestampUs + 3000);
    REQUIRE(timeline.remainder() == 0);

    const auto first = queue.front();
    REQUIRE(first.has_value());
    REQUIRE(first->kind == OutboundFrameKind::Data);
    REQUIRE(first->size == expected_channel_0.bytes.size());
    REQUIRE(std::equal(
        expected_channel_0.bytes.begin(), expected_channel_0.bytes.end(), first->data));
    queue.consume(first->size);

    const auto second = queue.front();
    REQUIRE(second.has_value());
    REQUIRE(second->kind == OutboundFrameKind::Data);
    REQUIRE(second->size == expected_channel_2.bytes.size());
    REQUIRE(std::equal(
        expected_channel_2.bytes.begin(), expected_channel_2.bytes.end(), second->data));
}

TEST_CASE("DATA segment leaves the timeline unchanged when a later channel lacks capacity",
          "[simulator][data-frame][segment][backpressure][timeline]")
{
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    const std::vector<float> samples{1.0F};
    const EncodeResult expected_first = ProtocolCodec::encode(
        make_data_frame(0, 0, kTimestampUs, samples));
    REQUIRE(expected_first.ok());

    SamplingContext context;
    context.start(1000, 0x05); // 通道 0 成功后，通道 2 因容量不足失败
    OutboundQueue queue(expected_first.bytes.size());
    UtcSampleTimeline timeline(1000, kTimestampUs);

    REQUIRE_FALSE(enqueue_segment_channels(context, queue, timeline, samples));
    REQUIRE(context.current_sequence(0) == 1);
    REQUIRE(context.current_sequence(2) == 0);
    REQUIRE(queue.queued_frames() == 1);
    REQUIRE(queue.queued_bytes() == expected_first.bytes.size());
    REQUIRE(timeline.current_ts_us() == kTimestampUs);
    REQUIRE(timeline.remainder() == 0);

    const auto queued = queue.front();
    REQUIRE(queued.has_value());
    REQUIRE(queued->size == expected_first.bytes.size());
    REQUIRE(std::equal(
        expected_first.bytes.begin(), expected_first.bytes.end(), queued->data));
}

TEST_CASE("DATA segment rejects an empty channel mask without side effects",
          "[simulator][data-frame][segment][validation]")
{
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    SamplingContext context;
    context.start(1000, 0x00);
    OutboundQueue queue(1024);
    UtcSampleTimeline timeline(1000, kTimestampUs);

    REQUIRE_FALSE(enqueue_segment_channels(
        context, queue, timeline, std::vector<float>{1.0F}));
    REQUIRE(context.current_sequence(0) == 0);
    REQUIRE(context.current_sequence(7) == 0);
    REQUIRE(queue.queued_bytes() == 0);
    REQUIRE(queue.queued_frames() == 0);
    REQUIRE(timeline.current_ts_us() == kTimestampUs);
    REQUIRE(timeline.remainder() == 0);
}

TEST_CASE("DATA segment validates its sample limit before the empty-mask branch",
          "[simulator][data-frame][segment][validation]")
{
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    SamplingContext context;
    context.start(1000, 0x00);
    OutboundQueue queue(64 * 1024);
    UtcSampleTimeline timeline(1000, kTimestampUs);
    const std::vector<float> too_many_samples(
        static_cast<std::size_t>(kMaxSamplesPerDataFrame) + 1,
        1.0F);

    REQUIRE_THROWS_AS(
        enqueue_segment_channels(context, queue, timeline, too_many_samples),
        std::invalid_argument);
    REQUIRE(context.current_sequence(0) == 0);
    REQUIRE(context.current_sequence(7) == 0);
    REQUIRE(queue.queued_bytes() == 0);
    REQUIRE(queue.queued_frames() == 0);
    REQUIRE(timeline.current_ts_us() == kTimestampUs);
    REQUIRE(timeline.remainder() == 0);
}

TEST_CASE("DATA tick accepts a legal empty budget without changing DATA state",
          "[simulator][data-frame][tick][low-rate]")
{
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    SamplingContext context;
    context.start(10, 0x01);
    OutboundQueue queue(1024);
    UtcSampleTimeline timeline(10, kTimestampUs);
    SampleBudget budget(10);

    REQUIRE(run_data_tick(context, queue, timeline, budget));
    REQUIRE(budget.credit() == 10);
    REQUIRE(context.current_sequence(0) == 0);
    REQUIRE(queue.queued_bytes() == 0);
    REQUIRE(queue.queued_frames() == 0);
    REQUIRE(timeline.current_ts_us() == kTimestampUs);
    REQUIRE(timeline.remainder() == 0);
}

TEST_CASE("DATA tick combines one budget segment with deterministic multi-channel samples",
          "[simulator][data-frame][tick][sequence][timeline]")
{
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    std::vector<float> expected_samples(20);
    for (std::uint16_t index = 0; index < expected_samples.size(); ++index)
    {
        expected_samples[index] = static_cast<float>(index % 256u);
    }

    const EncodeResult expected_channel_0 = ProtocolCodec::encode(
        make_data_frame(0, 0, kTimestampUs, expected_samples));
    const EncodeResult expected_channel_2 = ProtocolCodec::encode(
        make_data_frame(2, 0, kTimestampUs, expected_samples));
    REQUIRE(expected_channel_0.ok());
    REQUIRE(expected_channel_2.ok());

    SamplingContext context;
    context.start(1000, 0x05);
    OutboundQueue queue(4096);
    UtcSampleTimeline timeline(1000, kTimestampUs);
    SampleBudget budget(1000);

    REQUIRE(run_data_tick(context, queue, timeline, budget));
    REQUIRE(context.current_sequence(0) == 1);
    REQUIRE(context.current_sequence(2) == 1);
    REQUIRE(queue.queued_frames() == 2);
    REQUIRE(timeline.current_ts_us() == kTimestampUs + 20000);
    REQUIRE(timeline.remainder() == 0);

    const auto first = queue.front();
    REQUIRE(first.has_value());
    REQUIRE(first->size == expected_channel_0.bytes.size());
    REQUIRE(std::equal(
        expected_channel_0.bytes.begin(), expected_channel_0.bytes.end(), first->data));
    queue.consume(first->size);

    const auto second = queue.front();
    REQUIRE(second.has_value());
    REQUIRE(second->size == expected_channel_2.bytes.size());
    REQUIRE(std::equal(
        expected_channel_2.bytes.begin(), expected_channel_2.bytes.end(), second->data));
}

TEST_CASE("DATA tick processes every large-budget segment in order",
          "[simulator][data-frame][tick][segmentation][timeline]")
{
    constexpr std::uint32_t kSampleRate = 125000;
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    SamplingContext context;
    context.start(kSampleRate, 0x01);
    OutboundQueue queue(64 * 1024);
    UtcSampleTimeline timeline(kSampleRate, kTimestampUs);
    SampleBudget budget(kSampleRate);

    REQUIRE(run_data_tick(context, queue, timeline, budget));
    REQUIRE(context.current_sequence(0) == 3);
    REQUIRE(queue.queued_frames() == 3);
    REQUIRE(timeline.current_ts_us() == kTimestampUs + 20000);
    REQUIRE(timeline.remainder() == 0);

    const std::vector<std::uint16_t> segment_sizes{1020, 1020, 460};
    const std::vector<std::uint64_t> segment_timestamps{
        kTimestampUs,
        kTimestampUs + 8160,
        kTimestampUs + 16320,
    };

    for (std::size_t segment_index = 0; segment_index < segment_sizes.size(); ++segment_index)
    {
        std::vector<float> expected_samples(segment_sizes[segment_index]);
        for (std::uint16_t index = 0; index < segment_sizes[segment_index]; ++index)
        {
            expected_samples[index] = static_cast<float>(index % 256u);
        }

        const EncodeResult expected = ProtocolCodec::encode(make_data_frame(
            0,
            static_cast<std::uint32_t>(segment_index),
            segment_timestamps[segment_index],
            expected_samples));
        REQUIRE(expected.ok());

        const auto queued = queue.front();
        REQUIRE(queued.has_value());
        REQUIRE(queued->size == expected.bytes.size());
        REQUIRE(std::equal(expected.bytes.begin(), expected.bytes.end(), queued->data));
        queue.consume(queued->size);
    }
    REQUIRE(queue.empty());
}

TEST_CASE("DATA tick preserves committed earlier segments when a later segment lacks capacity",
          "[simulator][data-frame][tick][backpressure][timeline]")
{
    constexpr std::uint32_t kSampleRate = 125000;
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    std::vector<float> first_samples(kMaxSamplesPerDataFrame);
    for (std::uint16_t index = 0; index < kMaxSamplesPerDataFrame; ++index)
    {
        first_samples[index] = static_cast<float>(index % 256u);
    }
    const EncodeResult first_frame = ProtocolCodec::encode(
        make_data_frame(0, 0, kTimestampUs, first_samples));
    REQUIRE(first_frame.ok());

    SamplingContext context;
    context.start(kSampleRate, 0x01);
    OutboundQueue queue(first_frame.bytes.size());
    UtcSampleTimeline timeline(kSampleRate, kTimestampUs);
    SampleBudget budget(kSampleRate);

    REQUIRE_FALSE(run_data_tick(context, queue, timeline, budget));
    REQUIRE(budget.credit() == 0);
    REQUIRE(context.current_sequence(0) == 1);
    REQUIRE(queue.queued_frames() == 1);
    REQUIRE(queue.queued_bytes() == first_frame.bytes.size());
    REQUIRE(timeline.current_ts_us() == kTimestampUs + 8160);
    REQUIRE(timeline.remainder() == 0);

    const auto queued = queue.front();
    REQUIRE(queued.has_value());
    REQUIRE(queued->size == first_frame.bytes.size());
    REQUIRE(std::equal(first_frame.bytes.begin(), first_frame.bytes.end(), queued->data));
}

TEST_CASE("DATA tick rejects an empty channel mask after consuming a non-empty budget",
          "[simulator][data-frame][tick][validation]")
{
    constexpr std::uint64_t kTimestampUs = 1000000ULL;
    SamplingContext context;
    context.start(1000, 0x00);
    OutboundQueue queue(1024);
    UtcSampleTimeline timeline(1000, kTimestampUs);
    SampleBudget budget(1000);

    REQUIRE_FALSE(run_data_tick(context, queue, timeline, budget));
    REQUIRE(budget.credit() == 0);
    REQUIRE(context.current_sequence(0) == 0);
    REQUIRE(context.current_sequence(7) == 0);
    REQUIRE(queue.queued_bytes() == 0);
    REQUIRE(queue.queued_frames() == 0);
    REQUIRE(timeline.current_ts_us() == kTimestampUs);
    REQUIRE(timeline.remainder() == 0);
}

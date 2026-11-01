#include "catch_amalgamated.hpp"

#include "device_simulator/data_frame_builder.h"
#include "device_simulator/data_frame_limits.h"
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

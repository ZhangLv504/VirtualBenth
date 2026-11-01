#include "device_simulator/data_frame_builder.h"
#include "device_simulator/data_frame_limits.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include "protocol/protocol_codec.h"

namespace
{
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "DATA requires a 32-bit float representation");
    static_assert(std::numeric_limits<float>::is_iec559,
                  "DATA requires IEEE 754 binary32 floats");

    void append_u16_le(std::vector<std::uint8_t>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void append_u32_le(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (unsigned int byte = 0; byte < sizeof(value); ++byte)
        {
            bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
        }
    }

    void append_u64_le(std::vector<std::uint8_t>& bytes, std::uint64_t value)
    {
        for (unsigned int byte = 0; byte < sizeof(value); ++byte)
        {
            bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
        }
    }

    std::uint32_t float_bits(float value) noexcept
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
}

Frame make_data_frame(
    std::uint8_t channel_id,
    std::uint32_t sequence,
    std::uint64_t timestamp_us,
    const std::vector<float>& samples)
{
    if (samples.size() > kMaxSamplesPerDataFrame)
    {
        throw std::invalid_argument("DATA sample count exceeds 1020");
    }

    Frame frame;
    frame.command = Command::Data;
    frame.payload.reserve(15 + samples.size() * sizeof(float));

    frame.payload.push_back(channel_id);
    append_u32_le(frame.payload, sequence);
    append_u64_le(frame.payload, timestamp_us);
    append_u16_le(frame.payload, static_cast<std::uint16_t>(samples.size()));

    for (float sample : samples)
    {
        append_u32_le(frame.payload, float_bits(sample));
    }

    return frame;
}


bool enqueue_channel_data(
    SamplingContext& sampling_context, OutboundQueue& outbound_queue, std::uint8_t channel_id,
    std::uint64_t timestamp_us, const std::vector<float>& samples)
{
    Frame frame = make_data_frame(
        channel_id, sampling_context.current_sequence(channel_id), timestamp_us, samples
        );

    EncodeResult result = ProtocolCodec::encode(frame);
    if (result.ok())
    {
        if (outbound_queue.enqueue_data(std::move(result.bytes)))
        {
            sampling_context.commit_sequence(channel_id);
            return true;
        }
    }
    return false;
}

bool enqueue_segment_channels(SamplingContext& sampling_context, OutboundQueue& outbound_queue,
    UtcSampleTimeline& timeline, const std::vector<float>& samples)
{
    if (samples.size() > kMaxSamplesPerDataFrame)
    {
        throw std::invalid_argument("DATA segment sample count exceeds 1020");
    }

    const std::uint64_t timestamp_us = timeline.current_ts_us();

    if (sampling_context.channel_mask != 0)
    {
        for (std::uint32_t channel_id = 0; channel_id < 8; ++channel_id)
        {
            const auto bit = static_cast<std::uint8_t>(1u << channel_id);
            if ((sampling_context.channel_mask & bit) == 0)
            {
                continue;
            }

            if (!enqueue_channel_data(sampling_context, outbound_queue,
                channel_id, timestamp_us, samples))
            {
                return false;
            }
        }

        timeline.advance(static_cast<std::uint16_t>(samples.size()));
        return true;
    }
    return false;
}

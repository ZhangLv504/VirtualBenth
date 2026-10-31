#include "catch_amalgamated.hpp"

#include "device_simulator/data_frame_builder.h"
#include "protocol/protocol_codec.h"

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

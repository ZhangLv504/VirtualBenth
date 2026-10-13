#include "catch_amalgamated.hpp"
#include "protocol/protocol_codec.h"

#include <cstdint>
#include <vector>

// protocol 编码测试
TEST_CASE("HELLO frame matches protocol document example", "[protocol][encode][hello]") {
    Frame frame;
    frame.command = Command::Hello;
    frame.version = kProtocolVersion;

    const EncodeResult result = ProtocolCodec::encode(frame);
    const std::vector<std::uint8_t> expected{
        0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18
    };

    REQUIRE(result.ok());
    REQUIRE(result.bytes == expected);
}

TEST_CASE("START frame matches protocol document example", "[protocol][encode][start]")
{
    Frame frame{};
    frame.command = Command::Start;
    frame.payload = {0x10, 0x27, 0x00, 0x00, 0x07};

    const EncodeResult result = ProtocolCodec::encode(frame);
    const std::vector<std::uint8_t> expected{
        0xA5, 0x5A, 0x01, 0x02, 0x05, 0x00, 0x10,
        0x27, 0x00, 0x00, 0x07, 0x59, 0x0C,
    };

    REQUIRE(result.ok());
    REQUIRE(result.bytes == expected);
}

TEST_CASE("maximum payload is accepted", "[protocol][encode][boundary]")
{
    Frame frame{};
    frame.command = Command::Error;
    frame.payload.assign(4096, 0x10);

    const EncodeResult result = ProtocolCodec::encode(frame);

    REQUIRE(result.ok());
    REQUIRE(result.bytes.size() == 4104);
    REQUIRE(result.bytes[4] == 0x00);
    REQUIRE(result.bytes[5] == 0x10);
    REQUIRE(result.bytes[6] == 0x10);
    REQUIRE(result.bytes[result.bytes.size() - 3] == 0x10);
}

TEST_CASE("payload larger than 4096 bytes is rejected", "[protocol][encode][boundary]")
{
    Frame frame{};
    frame.command = Command::Error;
    frame.payload.assign(4097, 0x10);

    const EncodeResult result = ProtocolCodec::encode(frame);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == EncodeError::PayloadTooLarge);
    REQUIRE(result.bytes.empty());
}

TEST_CASE("Invalid command is rejected", "[protocol][encode][command]")
{
    Frame frame{};
    frame.command = Command::Invalid;
    const EncodeResult result = ProtocolCodec::encode(frame);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == EncodeError::InvalidCommand);
    REQUIRE(result.bytes.empty());
}

TEST_CASE("unknown command is rejected", "[protocol][encode][command]")
{
    Frame frame{};
    frame.command = static_cast<Command>(0x55);
    const EncodeResult result = ProtocolCodec::encode(frame);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == EncodeError::InvalidCommand);
    REQUIRE(result.bytes.empty());
}

TEST_CASE("unsupported version is rejected", "[protocol][encode][version]")
{
    Frame frame{};
    frame.command = Command::Start;
    frame.version = 0x02;
    const EncodeResult result = ProtocolCodec::encode(frame);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == EncodeError::UnsupportedVersion);
    REQUIRE(result.bytes.empty());
}

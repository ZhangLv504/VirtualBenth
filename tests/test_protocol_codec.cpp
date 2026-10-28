#include "catch_amalgamated.hpp"
#include "protocol/crc16.h"
#include "protocol/protocol_codec.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    // 测试修改版本、命令或 payload 后，按新内容重算帧尾 CRC。这样可以分别验证
    // “CRC 正确但业务值未知”和“CRC 本身损坏”，不会把两个失败原因混在一起。
    void rewrite_frame_crc(std::vector<std::uint8_t>& bytes)
    {
        const std::uint16_t payload_len = static_cast<std::uint16_t>(bytes[4])
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[5]) << 8);
        const std::size_t crc_offset = 6 + payload_len;
        const std::uint16_t crc = crc16_modbus(bytes.data() + 2, 4 + payload_len);
        bytes[crc_offset] = static_cast<std::uint8_t>(crc & 0xFF);
        bytes[crc_offset + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    }
} // namespace

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

// protocol 完整帧解码测试；流式拆包、粘包与重同步留给 StreamParser。
TEST_CASE("default decode result is fail-closed", "[protocol][decode][result]")
{
    const DecodeResult result;

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::IncompleteFrame);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("HELLO protocol example decodes to a Frame", "[protocol][decode][hello]")
{
    const std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18
    };

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.frame.version == kProtocolVersion);
    REQUIRE(result.frame.command == Command::Hello);
    REQUIRE(result.frame.payload.empty());
}

TEST_CASE("START protocol example decodes payload bytes", "[protocol][decode][start]")
{
    const std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x02, 0x05, 0x00, 0x10,
        0x27, 0x00, 0x00, 0x07, 0x59, 0x0C,
    };
    const std::vector<std::uint8_t> expected_payload{
        0x10, 0x27, 0x00, 0x00, 0x07,
    };

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.frame.version == kProtocolVersion);
    REQUIRE(result.frame.command == Command::Start);
    REQUIRE(result.frame.payload == expected_payload);
}

TEST_CASE("decode rejects an incomplete frame", "[protocol][decode][length]")
{
    const std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x02, 0x05, 0x00, 0x10,
    };

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::IncompleteFrame);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("decode rejects every input shorter than the fixed header", "[protocol][decode][length]")
{
    for (std::size_t size = 0; size < 6; ++size)
    {
        INFO("input size: " << size);
        const std::vector<std::uint8_t> bytes(size, 0x00);

        const DecodeResult result = ProtocolCodec::decode(bytes);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error == DecodeError::IncompleteFrame);
        REQUIRE(result.frame.command == Command::Invalid);
    }
}

TEST_CASE("decode rejects a frame with invalid magic", "[protocol][decode][magic]")
{
    const std::vector<std::uint8_t> bytes{
        0x00, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18
    };

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::InvalidMagic);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("decode rejects payload_len above 4096 without waiting for payload", "[protocol][decode][length]")
{
    const std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x04, 0x01, 0x10,
    };

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::PayloadTooLarge);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("decode accepts exactly one frame and rejects trailing bytes", "[protocol][decode][length]")
{
    const std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18, 0x00
    };

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::FrameSizeMismatch);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("decode rejects a CRC mismatch", "[protocol][decode][crc]")
{
    const std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x51, 0x18
    };

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::CrcMismatch);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("decode CRC covers a non-empty payload", "[protocol][decode][crc]")
{
    std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x02, 0x05, 0x00, 0x10,
        0x27, 0x00, 0x00, 0x07, 0x59, 0x0C,
    };
    bytes[6] ^= 0x01;

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::CrcMismatch);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("decode validates CRC before publishing an unsupported version", "[protocol][decode][crc][version]")
{
    std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18
    };
    bytes[2] = 0x02;

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error == DecodeError::CrcMismatch);
    REQUIRE(result.frame.command == Command::Invalid);
}

TEST_CASE("decode preserves an unsupported version for the business layer", "[protocol][decode][version]")
{
    std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    rewrite_frame_crc(bytes);

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.error == DecodeError::None);
    REQUIRE(result.frame.version == 0x02);
    REQUIRE(result.frame.command == Command::Hello);
    REQUIRE(result.frame.payload.empty());
}

TEST_CASE("decode preserves an unknown command for the business layer", "[protocol][decode][command]")
{
    std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x55, 0x00, 0x00, 0x00, 0x00
    };
    rewrite_frame_crc(bytes);

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.error == DecodeError::None);
    REQUIRE(result.frame.version == kProtocolVersion);
    REQUIRE(static_cast<std::uint8_t>(result.frame.command) == 0x55);
    REQUIRE(result.frame.payload.empty());
}

TEST_CASE("decode preserves zero command for the business layer", "[protocol][decode][command]")
{
    std::vector<std::uint8_t> bytes{
        0xA5, 0x5A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    rewrite_frame_crc(bytes);

    const DecodeResult result = ProtocolCodec::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.error == DecodeError::None);
    REQUIRE(result.frame.version == kProtocolVersion);
    REQUIRE(result.frame.command == Command::Invalid);
    REQUIRE(result.frame.payload.empty());
}

TEST_CASE("maximum payload survives encode and decode round-trip", "[protocol][decode][boundary]")
{
    Frame frame{};
    frame.command = Command::Error;
    frame.payload.assign(4096, 0x10);

    const EncodeResult encoded = ProtocolCodec::encode(frame);
    REQUIRE(encoded.ok());

    const DecodeResult decoded = ProtocolCodec::decode(encoded.bytes);

    REQUIRE(decoded.ok());
    REQUIRE(decoded.frame.version == frame.version);
    REQUIRE(decoded.frame.command == frame.command);
    REQUIRE(decoded.frame.payload == frame.payload);
}

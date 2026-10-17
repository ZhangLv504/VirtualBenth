#include "catch_amalgamated.hpp"

#include "protocol/stream_parser.h"
#include "protocol/protocol_codec.h"

#include <cstddef>
#include <cstdint>
#include <vector>

TEST_CASE("stream_parser protocol example buffer and reset", "[protocol][feed][buffer]")
{
    StreamParser stream_parser;
    const std::vector<Frame> frames = stream_parser.feed({});
    REQUIRE(frames.empty());
    REQUIRE(stream_parser.feed({0xA5}).empty());
    REQUIRE(stream_parser.buffered_size() == 1);
    stream_parser.reset();
    REQUIRE(stream_parser.buffered_size() == 0);
}

TEST_CASE("stream_parser protocol example Hello", "[protocol][feed][Hello]")
{
    StreamParser stream_parser;
    const std::vector<std::uint8_t> hello_chunk = {0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18};
    for (std::size_t i = 0; i < hello_chunk.size(); ++i)
    {
        if (i != hello_chunk.size() - 1)
        {
            const std::vector<Frame> frames = stream_parser.feed({hello_chunk[i]});
            REQUIRE(frames.empty());
        }
        else
        {
            const std::vector<Frame> frames = stream_parser.feed({hello_chunk[i]});
            REQUIRE(frames.size() == 1);
            REQUIRE(frames[0].command == Command::Hello);
            REQUIRE(frames[0].version == kProtocolVersion);
            REQUIRE(frames[0].payload.empty());
            REQUIRE(stream_parser.buffered_size() == 0);
        }
    }
}

TEST_CASE("stream_parser protocol example Hello and START", "[protocol][feed][Hello_START]")
{
    StreamParser stream_parser;
    const std::vector<std::uint8_t> hello = {0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18};
    const std::vector<std::uint8_t> start = {0xA5, 0x5A, 0x01, 0x02, 0x05, 0x00, 0x10, 0x27, 0x00, 0x00, 0x07, 0x59, 0x0C};
    std::vector<std::uint8_t> chunk;
    chunk.insert(chunk.end(), hello.begin(), hello.end());
    chunk.insert(chunk.end(), start.begin(), start.end());
    const std::vector<Frame> frames = stream_parser.feed(chunk);

    REQUIRE(frames.size() == 2);

    REQUIRE(frames[0].command == Command::Hello);
    REQUIRE(frames[0].version == kProtocolVersion);
    REQUIRE(frames[0].payload.empty());

    REQUIRE(frames[1].command == Command::Start);
    REQUIRE(frames[1].version == kProtocolVersion);
    const std::vector<std::uint8_t> start_payload = {0x10, 0x27, 0x00, 0x00, 0x07};
    REQUIRE(frames[1].payload == start_payload);
    REQUIRE(stream_parser.buffered_size() == 0);
}

// 噪声前缀后解析 HELLO
TEST_CASE("stream_parser skips a noise prefix with partial magic", "[protocol][feed][noise][resync]")
{
    StreamParser stream_parser;
    const std::vector<std::uint8_t> noise = {0x00, 0xA5};
    const std::vector<std::uint8_t> hello = {0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18};
    std::vector<std::uint8_t> chunk;
    chunk.insert(chunk.end(), noise.begin(), noise.end());
    chunk.insert(chunk.end(), hello.begin(), hello.end());
    const std::vector<Frame> frames = stream_parser.feed(chunk);

    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].command == Command::Hello);
    REQUIRE(frames[0].version == kProtocolVersion);
    REQUIRE(frames[0].payload.empty());
    REQUIRE(stream_parser.buffered_size() == 0);
}

// CRC 错帧后恢复到 START
TEST_CASE("stream_parser resynchronizes after a CRC error", "[protocol][feed][crc][resync]")
{
    StreamParser stream_parser;
    std::vector<std::uint8_t> invalid_hello = {0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18};
    invalid_hello.back() ^= 0x01;
    const std::vector<std::uint8_t> start = {0xA5, 0x5A, 0x01, 0x02, 0x05, 0x00, 0x10, 0x27, 0x00, 0x00, 0x07, 0x59, 0x0C};
    const std::vector<std::uint8_t> expected_payload = {0x10, 0x27, 0x00, 0x00, 0x07};
    std::vector<std::uint8_t> chunk;
    chunk.insert(chunk.end(), invalid_hello.begin(), invalid_hello.end());
    chunk.insert(chunk.end(), start.begin(), start.end());
    const std::vector<Frame> frames = stream_parser.feed(chunk);

    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].command == Command::Start);
    REQUIRE(frames[0].version == kProtocolVersion);
    REQUIRE(frames[0].payload == expected_payload);
    REQUIRE(stream_parser.buffered_size() == 0);
}

// 超长长度头后恢复到 HELLO
TEST_CASE("stream_parser resynchronizes after an oversized payload length", "[protocol][feed][length][resync]")
{
    StreamParser stream_parser;
    const std::vector<std::uint8_t> invalid = {0xA5, 0x5A, 0x01, 0x02, 0x01, 0x10};
    const std::vector<std::uint8_t> hello = {0xA5, 0x5A, 0x01, 0x01, 0x00, 0x00, 0x50, 0x18};
    std::vector<std::uint8_t> chunk;
    chunk.insert(chunk.end(), invalid.begin(), invalid.end());
    chunk.insert(chunk.end(), hello.begin(), hello.end());
    const std::vector<Frame> frames = stream_parser.feed(chunk);

    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].command == Command::Hello);
    REQUIRE(frames[0].version == kProtocolVersion);
    REQUIRE(frames[0].payload.empty());
    REQUIRE(stream_parser.buffered_size() == 0);
}

// 增加一个最大合法帧的临界拆包测试：4103B 时必须等待，4104B 时必须成功解析
TEST_CASE("stream_parser preserves the maximum incomplete frame", "[protocol][feed][boundary][max]")
{
    Frame frame;
    frame.command = Command::Error;
    frame.payload.assign(4096, 0x20);
    frame.payload[0] = 0x05; // ERROR(INTERNAL) 的小端低字节
    frame.payload[1] = 0x00;
    frame.payload.back() = 0xBB;

    const EncodeResult result = ProtocolCodec::encode(frame);
    REQUIRE(result.ok());
    REQUIRE(result.bytes.size() == 4104);

    const std::vector<std::uint8_t> first_chunk(result.bytes.begin(), result.bytes.end() - 1);
    const std::vector<std::uint8_t> final_chunk{result.bytes.back()};
    StreamParser stream_parser;

    const std::vector<Frame> incomplete_frames = stream_parser.feed(first_chunk);
    REQUIRE(incomplete_frames.empty());
    REQUIRE(stream_parser.buffered_size() == 4103);

    const std::vector<Frame> frames = stream_parser.feed(final_chunk);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].command == Command::Error);
    REQUIRE(frames[0].version == kProtocolVersion);
    REQUIRE(frames[0].payload.size() == 4096);
    REQUIRE(frames[0].payload == frame.payload);
    REQUIRE(frames[0].payload.front() == 0x05);
    REQUIRE(frames[0].payload.back() == 0xBB);
    REQUIRE(stream_parser.buffered_size() == 0);
}

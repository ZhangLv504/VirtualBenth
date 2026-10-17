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

TEST_CASE("stream_parser handles reproducible uneven fragmentation across frames",
          "[protocol][feed][fragmentation][magic]")
{
    Frame hello;
    hello.command = Command::Hello;

    Frame start;
    start.command = Command::Start;
    // sample_rate = 0x00005AA5，chan_mask = 0x01；payload 中的 A5 5A 不是帧起点。
    start.payload = {0xA5, 0x5A, 0x00, 0x00, 0x01};

    Frame stop;
    stop.command = Command::Stop;

    const EncodeResult hello_result = ProtocolCodec::encode(hello);
    const EncodeResult start_result = ProtocolCodec::encode(start);
    const EncodeResult stop_result = ProtocolCodec::encode(stop);
    REQUIRE(hello_result.ok());
    REQUIRE(start_result.ok());
    REQUIRE(stop_result.ok());
    REQUIRE(hello_result.bytes.size() == 8);
    REQUIRE(start_result.bytes.size() == 13);
    REQUIRE(stop_result.bytes.size() == 8);

    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), hello_result.bytes.begin(), hello_result.bytes.end());
    stream.insert(stream.end(), start_result.bytes.begin(), start_result.bytes.end());
    stream.insert(stream.end(), stop_result.bytes.begin(), stop_result.bytes.end());

    // 固定分块既可复现，又会跨越帧边界，并把 START payload 中的 A5 与 5A 分到两次 feed。
    const std::vector<std::size_t> chunk_sizes = {1, 4, 2, 7, 1, 5, 3, 6};
    StreamParser stream_parser;
    std::vector<Frame> parsed_frames;
    std::size_t offset = 0;
    std::size_t chunk_index = 0;

    while (offset < stream.size())
    {
        const std::size_t remaining = stream.size() - offset;
        const std::size_t requested_size = chunk_sizes[chunk_index % chunk_sizes.size()];
        const std::size_t chunk_size = requested_size < remaining ? requested_size : remaining;
        const std::vector<std::uint8_t> chunk(stream.begin() + offset,
                                              stream.begin() + offset + chunk_size);
        const std::vector<Frame> frames = stream_parser.feed(chunk);
        parsed_frames.insert(parsed_frames.end(), frames.begin(), frames.end());
        offset += chunk_size;
        ++chunk_index;
    }

    REQUIRE(offset == stream.size());
    REQUIRE(parsed_frames.size() == 3);

    REQUIRE(parsed_frames[0].version == kProtocolVersion);
    REQUIRE(parsed_frames[0].command == Command::Hello);
    REQUIRE(parsed_frames[0].payload == hello.payload);

    REQUIRE(parsed_frames[1].version == kProtocolVersion);
    REQUIRE(parsed_frames[1].command == Command::Start);
    REQUIRE(parsed_frames[1].payload == start.payload);

    REQUIRE(parsed_frames[2].version == kProtocolVersion);
    REQUIRE(parsed_frames[2].command == Command::Stop);
    REQUIRE(parsed_frames[2].payload == stop.payload);
    REQUIRE(stream_parser.buffered_size() == 0);
}

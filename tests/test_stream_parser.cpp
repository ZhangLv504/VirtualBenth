#include "catch_amalgamated.hpp"

#include "protocol/stream_parser.h"

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

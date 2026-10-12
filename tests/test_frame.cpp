#include "catch_amalgamated.hpp"

#include "protocol/frame.h"

#include <cstdint>
#include <vector>

TEST_CASE("command values match protocol", "[frame][command]") {
    REQUIRE(static_cast<std::uint8_t>(Command::Invalid) == 0x00);
    REQUIRE(static_cast<std::uint8_t>(Command::Hello) == 0x01);
    REQUIRE(static_cast<std::uint8_t>(Command::Start) == 0x02);
    REQUIRE(static_cast<std::uint8_t>(Command::Stop) == 0x03);
    REQUIRE(static_cast<std::uint8_t>(Command::Data) == 0x04);
    REQUIRE(static_cast<std::uint8_t>(Command::Status) == 0x05);
    REQUIRE(static_cast<std::uint8_t>(Command::Ack) == 0x06);
    REQUIRE(static_cast<std::uint8_t>(Command::Error) == 0x7F);
}

TEST_CASE("default frame has a safe state", "[frame]")
{
    const Frame frame{};

    REQUIRE(frame.version == kProtocolVersion);
    REQUIRE(frame.command == Command::Invalid);
    REQUIRE(frame.payload.empty());
}

TEST_CASE("frame stores payload bytes", "[frame][payload]")
{
    Frame frame{};
    frame.command = Command::Data;

    const std::vector<std::uint8_t> expected{
        0xA5, 0x5A, 0x01, 0xFF
    };

    frame.payload = expected;

    REQUIRE(frame.command == Command::Data);
    REQUIRE(frame.payload == expected);
    REQUIRE(frame.payload.size() == 4);
}

#include "catch_amalgamated.hpp"

#include "device_simulator/utc_sample_timeline.h"

#include <cstdint>
#include <stdexcept>

TEST_CASE("UTC sample timeline preserves its explicit starting point",
          "[simulator][utc-sample-timeline][initial-state]")
{
    constexpr std::uint64_t kStartTsUs = 1712345678000ULL;
    UtcSampleTimeline timeline(1000, kStartTsUs);

    REQUIRE(timeline.sample_rate() == 1000);
    REQUIRE(timeline.current_ts_us() == kStartTsUs);
    REQUIRE(timeline.remainder() == 0);
    REQUIRE_THROWS_AS(UtcSampleTimeline(0, kStartTsUs), std::invalid_argument);
}

TEST_CASE("UTC sample timeline advances an exactly divisible duration",
          "[simulator][utc-sample-timeline][exact]")
{
    constexpr std::uint64_t kStartTsUs = 1712345678000ULL;
    UtcSampleTimeline timeline(1000, kStartTsUs);

    timeline.advance(3);
    REQUIRE(timeline.current_ts_us() == kStartTsUs + 3000);
    REQUIRE(timeline.remainder() == 0);

    timeline.advance(0);
    REQUIRE(timeline.current_ts_us() == kStartTsUs + 3000);
    REQUIRE(timeline.remainder() == 0);
}

TEST_CASE("UTC sample timeline carries a fractional remainder across segments",
          "[simulator][utc-sample-timeline][remainder]")
{
    constexpr std::uint64_t kStartTsUs = 1000000;
    UtcSampleTimeline timeline(3, kStartTsUs);

    timeline.advance(1);
    REQUIRE(timeline.current_ts_us() == kStartTsUs + 333333);
    REQUIRE(timeline.remainder() == 1);

    timeline.advance(1);
    REQUIRE(timeline.current_ts_us() == kStartTsUs + 666666);
    REQUIRE(timeline.remainder() == 2);

    timeline.advance(1);
    REQUIRE(timeline.current_ts_us() == kStartTsUs + 1000000);
    REQUIRE(timeline.remainder() == 0);
}

TEST_CASE("UTC sample timeline reset is validated before state changes",
          "[simulator][utc-sample-timeline][reset]")
{
    UtcSampleTimeline timeline(3, 100);
    timeline.advance(1);

    REQUIRE_THROWS_AS(timeline.reset(0, 999), std::invalid_argument);
    REQUIRE(timeline.sample_rate() == 3);
    REQUIRE(timeline.current_ts_us() == 333433);
    REQUIRE(timeline.remainder() == 1);

    timeline.reset(4, 2000);
    REQUIRE(timeline.sample_rate() == 4);
    REQUIRE(timeline.current_ts_us() == 2000);
    REQUIRE(timeline.remainder() == 0);

    timeline.advance(1);
    REQUIRE(timeline.current_ts_us() == 252000);
    REQUIRE(timeline.remainder() == 0);
}

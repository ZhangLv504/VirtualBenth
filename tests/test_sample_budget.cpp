#include "catch_amalgamated.hpp"

#include "device_simulator/sample_budget.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>

TEST_CASE("sample budget accumulates rates below the tick frequency",
          "[simulator][sample-budget][low-rate]")
{
    SampleBudget budget(10);

    for (int tick = 0; tick < 4; ++tick)
    {
        const TickSampleBudget result = budget.next_tick();
        REQUIRE(result.total_samples == 0);
        REQUIRE(result.segments.empty());
    }

    const TickSampleBudget fifth = budget.next_tick();
    REQUIRE(fifth.total_samples == 1);
    REQUIRE(fifth.segments == std::vector<std::uint16_t>{1});
    REQUIRE(budget.credit() == 0);
}

TEST_CASE("sample budget preserves a non-divisible rate over one second",
          "[simulator][sample-budget][remainder]")
{
    SampleBudget budget(10001);
    std::uint64_t total = 0;

    for (std::uint32_t tick = 0; tick < kDataTicksPerSecond; ++tick)
    {
        const TickSampleBudget result = budget.next_tick();
        total += result.total_samples;
        REQUIRE(std::accumulate(
                    result.segments.begin(),
                    result.segments.end(),
                    std::uint32_t{0}) == result.total_samples);
    }

    REQUIRE(total == 10001);
    REQUIRE(budget.credit() == 0);
}

TEST_CASE("sample budget splits a large tick without losing samples",
          "[simulator][sample-budget][segmentation]")
{
    constexpr std::uint32_t kSamplesThisTick = 2500;
    SampleBudget budget(kSamplesThisTick * kDataTicksPerSecond);

    const TickSampleBudget result = budget.next_tick();
    REQUIRE(result.total_samples == kSamplesThisTick);
    REQUIRE((result.segments == std::vector<std::uint16_t>{1020, 1020, 460}));
    REQUIRE(std::all_of(
        result.segments.begin(),
        result.segments.end(),
        [](std::uint16_t count) { return count <= kMaxSamplesPerDataFrame; }));
    REQUIRE(std::accumulate(
                result.segments.begin(),
                result.segments.end(),
                std::uint32_t{0}) == result.total_samples);
}

TEST_CASE("sample budget rejects a zero sample rate",
          "[simulator][sample-budget][validation]")
{
    REQUIRE_THROWS_AS(SampleBudget(0), std::invalid_argument);
}

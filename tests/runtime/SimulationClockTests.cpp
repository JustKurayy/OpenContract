#include "TestSupport.hpp"

#include <contract/runtime/SimulationClock.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

int main() {
    using namespace std::chrono_literals;
    using contract::runtime::SimulationClockErrorCode;

    const auto invalid_step =
        contract::runtime::FixedStepClock::create(0ns, std::size_t{3});
    CONTRACT_EXPECT(!invalid_step.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_step.error().code,
        SimulationClockErrorCode::invalid_step);

    const auto invalid_limit =
        contract::runtime::FixedStepClock::create(10ms, std::size_t{0});
    CONTRACT_EXPECT(!invalid_limit.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_limit.error().code,
        SimulationClockErrorCode::invalid_catch_up_limit);

    auto created =
        contract::runtime::FixedStepClock::create(10ms, std::size_t{3});
    CONTRACT_EXPECT(created.has_value());
    auto clock = created.value();

    const auto partial = clock.advance(5ms);
    CONTRACT_EXPECT(partial.has_value());
    CONTRACT_EXPECT_EQ(partial.value().tick_count, std::size_t{0});
    CONTRACT_EXPECT_EQ(partial.value().remainder, 5ms);
    CONTRACT_EXPECT_EQ(partial.value().discarded, 0ns);
    CONTRACT_EXPECT_EQ(clock.completed_ticks(), std::uint64_t{0});

    const auto one_tick = clock.advance(10ms);
    CONTRACT_EXPECT(one_tick.has_value());
    CONTRACT_EXPECT_EQ(one_tick.value().first_tick, std::uint64_t{0});
    CONTRACT_EXPECT_EQ(one_tick.value().tick_count, std::size_t{1});
    CONTRACT_EXPECT_EQ(one_tick.value().remainder, 5ms);
    CONTRACT_EXPECT_EQ(clock.completed_ticks(), std::uint64_t{1});

    const auto capped = clock.advance(100ms);
    CONTRACT_EXPECT(capped.has_value());
    CONTRACT_EXPECT_EQ(capped.value().first_tick, std::uint64_t{1});
    CONTRACT_EXPECT_EQ(capped.value().tick_count, std::size_t{3});
    CONTRACT_EXPECT_EQ(capped.value().discarded, 70ms);
    CONTRACT_EXPECT_EQ(capped.value().remainder, 5ms);
    CONTRACT_EXPECT_EQ(clock.completed_ticks(), std::uint64_t{4});

    const auto before_negative = clock.completed_ticks();
    const auto negative = clock.advance(-1ns);
    CONTRACT_EXPECT(!negative.has_value());
    CONTRACT_EXPECT_EQ(
        negative.error().code,
        SimulationClockErrorCode::negative_elapsed_time);
    CONTRACT_EXPECT_EQ(clock.completed_ticks(), before_negative);
    CONTRACT_EXPECT_EQ(clock.remainder(), 5ms);

    const auto maximum_duration =
        std::chrono::nanoseconds(std::numeric_limits<std::int64_t>::max());
    auto overflow_clock_result =
        contract::runtime::FixedStepClock::create(
            maximum_duration,
            std::size_t{1});
    CONTRACT_EXPECT(overflow_clock_result.has_value());
    auto overflow_clock = overflow_clock_result.value();
    const auto almost_maximum = overflow_clock.advance(
        maximum_duration - 1ns);
    CONTRACT_EXPECT(almost_maximum.has_value());
    const auto overflow = overflow_clock.advance(2ns);
    CONTRACT_EXPECT(!overflow.has_value());
    CONTRACT_EXPECT_EQ(
        overflow.error().code,
        SimulationClockErrorCode::elapsed_time_overflow);
    CONTRACT_EXPECT_EQ(
        overflow_clock.remainder(),
        maximum_duration - 1ns);

    return contract::test::finish();
}

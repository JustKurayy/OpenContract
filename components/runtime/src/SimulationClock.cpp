#include <contract/runtime/SimulationClock.hpp>

#include <algorithm>
#include <limits>

namespace contract::runtime {

FixedStepClock::FixedStepClock(
    std::chrono::nanoseconds step,
    std::size_t maximum_catch_up_ticks)
    : step_(step),
      maximum_catch_up_ticks_(maximum_catch_up_ticks) {}

core::Result<FixedStepClock, SimulationClockError> FixedStepClock::create(
    std::chrono::nanoseconds step,
    std::size_t maximum_catch_up_ticks) {
    if (step.count() <= 0) {
        return core::Result<FixedStepClock, SimulationClockError>::failure(
            {
                SimulationClockErrorCode::invalid_step,
                "Simulation step must be positive"
            });
    }
    if (maximum_catch_up_ticks == 0) {
        return core::Result<FixedStepClock, SimulationClockError>::failure(
            {
                SimulationClockErrorCode::invalid_catch_up_limit,
                "Maximum catch-up ticks must be positive"
            });
    }
    return core::Result<FixedStepClock, SimulationClockError>::success(
        FixedStepClock(step, maximum_catch_up_ticks));
}

core::Result<SimulationAdvance, SimulationClockError> FixedStepClock::advance(
    std::chrono::nanoseconds elapsed) {
    if (elapsed.count() < 0) {
        return core::Result<SimulationAdvance, SimulationClockError>::failure(
            {
                SimulationClockErrorCode::negative_elapsed_time,
                "Elapsed simulation time cannot be negative"
            });
    }
    if (elapsed.count() >
        std::numeric_limits<std::int64_t>::max() - remainder_.count()) {
        return core::Result<SimulationAdvance, SimulationClockError>::failure(
            {
                SimulationClockErrorCode::elapsed_time_overflow,
                "Accumulated simulation time would overflow"
            });
    }

    const auto total_count = remainder_.count() + elapsed.count();
    const auto available_ticks = static_cast<std::uint64_t>(
        total_count / step_.count());
    const auto executed_ticks = static_cast<std::size_t>(
        std::min<std::uint64_t>(
            available_ticks,
            maximum_catch_up_ticks_));
    if (executed_ticks >
        std::numeric_limits<std::uint64_t>::max() - completed_ticks_) {
        return core::Result<SimulationAdvance, SimulationClockError>::failure(
            {
                SimulationClockErrorCode::completed_tick_overflow,
                "Completed simulation tick counter would overflow"
            });
    }

    const auto discarded_ticks =
        available_ticks - static_cast<std::uint64_t>(executed_ticks);
    const auto discarded_count =
        static_cast<std::int64_t>(discarded_ticks) * step_.count();
    const auto executed_count =
        static_cast<std::int64_t>(executed_ticks) * step_.count();
    const auto remainder_count =
        total_count - discarded_count - executed_count;

    const SimulationAdvance result{
        completed_ticks_,
        executed_ticks,
        std::chrono::nanoseconds(remainder_count),
        std::chrono::nanoseconds(discarded_count)};
    completed_ticks_ += static_cast<std::uint64_t>(executed_ticks);
    remainder_ = result.remainder;
    return core::Result<SimulationAdvance, SimulationClockError>::success(
        result);
}

std::chrono::nanoseconds FixedStepClock::step() const noexcept {
    return step_;
}

std::chrono::nanoseconds FixedStepClock::remainder() const noexcept {
    return remainder_;
}

std::size_t FixedStepClock::maximum_catch_up_ticks() const noexcept {
    return maximum_catch_up_ticks_;
}

std::uint64_t FixedStepClock::completed_ticks() const noexcept {
    return completed_ticks_;
}

}

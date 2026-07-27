#pragma once

#include <contract/core/Result.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace contract::runtime {

enum class SimulationClockErrorCode {
    invalid_step,
    invalid_catch_up_limit,
    negative_elapsed_time,
    elapsed_time_overflow,
    completed_tick_overflow
};

struct SimulationClockError {
    SimulationClockErrorCode code{SimulationClockErrorCode::invalid_step};
    std::string message;
};

struct SimulationAdvance {
    std::uint64_t first_tick{0};
    std::size_t tick_count{0};
    std::chrono::nanoseconds remainder{0};
    std::chrono::nanoseconds discarded{0};
};

class FixedStepClock {
public:
    [[nodiscard]] static core::Result<FixedStepClock, SimulationClockError> create(
        std::chrono::nanoseconds step,
        std::size_t maximum_catch_up_ticks);

    [[nodiscard]] core::Result<SimulationAdvance, SimulationClockError> advance(
        std::chrono::nanoseconds elapsed);

    [[nodiscard]] std::chrono::nanoseconds step() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds remainder() const noexcept;
    [[nodiscard]] std::size_t maximum_catch_up_ticks() const noexcept;
    [[nodiscard]] std::uint64_t completed_ticks() const noexcept;

private:
    FixedStepClock(
        std::chrono::nanoseconds step,
        std::size_t maximum_catch_up_ticks);

    std::chrono::nanoseconds step_;
    std::chrono::nanoseconds remainder_{0};
    std::size_t maximum_catch_up_ticks_{0};
    std::uint64_t completed_ticks_{0};
};

}

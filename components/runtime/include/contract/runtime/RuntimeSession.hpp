#pragma once

#include <contract/core/Result.hpp>
#include <contract/runtime/RuntimeCommand.hpp>
#include <contract/runtime/RuntimeEventJournal.hpp>
#include <contract/runtime/RuntimeSystem.hpp>
#include <contract/runtime/RuntimeWorld.hpp>
#include <contract/runtime/SimulationClock.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace contract::runtime {

inline constexpr std::size_t default_runtime_event_capacity = 4096;

enum class RuntimeSessionErrorCode {
    invalid_clock,
    pending_command_limit_exceeded,
    clock_advance_failed,
    command_batch_failed,
    system_evaluation_failed,
    system_command_batch_failed,
    event_journal_failed
};

struct RuntimeSessionError {
    RuntimeSessionErrorCode code{RuntimeSessionErrorCode::invalid_clock};
    std::optional<SimulationClockError> clock_error;
    std::optional<RuntimeCommandError> command_error;
    std::optional<RuntimeSystemError> system_error;
    std::optional<RuntimeEventJournalError> event_journal_error;
    std::string message;
};

struct RuntimeSessionAdvance {
    SimulationAdvance timing;
    std::size_t commands_applied{0};
    std::vector<RuntimeEvent> events;
};

class RuntimeSession {
public:
    [[nodiscard]] static core::Result<RuntimeSession, RuntimeSessionError> create(
        RuntimeWorld world,
        std::chrono::nanoseconds simulation_step,
        std::size_t maximum_catch_up_ticks,
        std::size_t maximum_pending_commands,
        std::size_t maximum_retained_events =
            default_runtime_event_capacity,
        std::size_t maximum_system_commands_per_tick =
            default_runtime_command_limit);

    [[nodiscard]] core::Result<void, RuntimeSessionError> enqueue(
        RuntimeCommand command);
    [[nodiscard]] core::Result<RuntimeSessionAdvance, RuntimeSessionError> advance(
        std::chrono::nanoseconds elapsed);
    [[nodiscard]] core::Result<RuntimeSessionAdvance, RuntimeSessionError> advance(
        std::chrono::nanoseconds elapsed,
        std::span<const RuntimeSystem* const> systems);

    void clear_pending_commands() noexcept;
    [[nodiscard]] std::vector<SequencedRuntimeEvent> drain_events();

    [[nodiscard]] const RuntimeWorld& world() const noexcept;
    [[nodiscard]] std::span<const SequencedRuntimeEvent>
    retained_events() const noexcept;
    [[nodiscard]] std::size_t pending_command_count() const noexcept;
    [[nodiscard]] std::uint64_t completed_ticks() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds clock_remainder() const noexcept;

private:
    RuntimeSession(
        RuntimeWorld world,
        FixedStepClock clock,
        std::size_t maximum_pending_commands,
        std::size_t maximum_retained_events,
        std::size_t maximum_system_commands_per_tick);

    RuntimeWorld world_;
    FixedStepClock clock_;
    std::size_t maximum_pending_commands_{0};
    std::size_t maximum_system_commands_per_tick_{0};
    std::vector<RuntimeCommand> pending_commands_;
    RuntimeCommandProcessor command_processor_;
    RuntimeSystemCoordinator system_coordinator_;
    RuntimeEventJournal event_journal_;
};

}

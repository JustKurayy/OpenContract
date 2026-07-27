#include <contract/runtime/RuntimeSession.hpp>

#include <utility>

namespace contract::runtime {

RuntimeSession::RuntimeSession(
    RuntimeWorld world,
    FixedStepClock clock,
    std::size_t maximum_pending_commands,
    std::size_t maximum_retained_events,
    std::size_t maximum_system_commands_per_tick)
    : world_(std::move(world)),
      clock_(std::move(clock)),
      maximum_pending_commands_(maximum_pending_commands),
      maximum_system_commands_per_tick_(maximum_system_commands_per_tick),
      event_journal_(maximum_retained_events) {}

core::Result<RuntimeSession, RuntimeSessionError> RuntimeSession::create(
    RuntimeWorld world,
    std::chrono::nanoseconds simulation_step,
    std::size_t maximum_catch_up_ticks,
    std::size_t maximum_pending_commands,
    std::size_t maximum_retained_events,
    std::size_t maximum_system_commands_per_tick) {
    auto clock = FixedStepClock::create(
        simulation_step,
        maximum_catch_up_ticks);
    if (!clock.has_value()) {
        return core::Result<RuntimeSession, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::invalid_clock,
                clock.error(),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                clock.error().message
            });
    }
    return core::Result<RuntimeSession, RuntimeSessionError>::success(
        RuntimeSession(
            std::move(world),
            std::move(clock.value()),
            maximum_pending_commands,
            maximum_retained_events,
            maximum_system_commands_per_tick));
}

core::Result<void, RuntimeSessionError> RuntimeSession::enqueue(
    RuntimeCommand command) {
    if (pending_commands_.size() >= maximum_pending_commands_) {
        return core::Result<void, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::pending_command_limit_exceeded,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "Pending runtime command limit was reached"
            });
    }
    pending_commands_.push_back(std::move(command));
    return core::Result<void, RuntimeSessionError>::success();
}

core::Result<RuntimeSessionAdvance, RuntimeSessionError> RuntimeSession::advance(
    std::chrono::nanoseconds elapsed) {
    return advance(elapsed, {});
}

core::Result<RuntimeSessionAdvance, RuntimeSessionError> RuntimeSession::advance(
    std::chrono::nanoseconds elapsed,
    std::span<const RuntimeSystem* const> systems) {
    auto candidate_clock = clock_;
    auto timing = candidate_clock.advance(elapsed);
    if (!timing.has_value()) {
        return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::clock_advance_failed,
                timing.error(),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                timing.error().message
            });
    }

    if (timing.value().tick_count == 0) {
        clock_ = std::move(candidate_clock);
        return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::success(
            {timing.value(), 0, {}});
    }

    auto candidate_world = world_;
    std::vector<RuntimeEvent> events;
    std::size_t command_count = pending_commands_.size();

    const auto pending_result = command_processor_.apply_atomic(
        candidate_world,
        pending_commands_,
        maximum_pending_commands_);
    if (!pending_result.has_value()) {
        return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::command_batch_failed,
                std::nullopt,
                pending_result.error(),
                std::nullopt,
                std::nullopt,
                pending_result.error().message
            });
    }
    events.insert(
        events.end(),
        pending_result.value().events.begin(),
        pending_result.value().events.end());

    for (std::size_t tick_offset = 0;
         tick_offset < timing.value().tick_count;
         ++tick_offset) {
        const RuntimeSystemContext context{
            timing.value().first_tick +
                static_cast<std::uint64_t>(tick_offset),
            candidate_clock.step()
        };
        auto system_commands = system_coordinator_.evaluate(
            candidate_world,
            context,
            systems,
            maximum_system_commands_per_tick_);
        if (!system_commands.has_value()) {
            return core::Result<
                RuntimeSessionAdvance,
                RuntimeSessionError>::failure(
                {
                    RuntimeSessionErrorCode::system_evaluation_failed,
                    std::nullopt,
                    std::nullopt,
                    system_commands.error(),
                    std::nullopt,
                    system_commands.error().message
                });
        }

        const auto system_result = command_processor_.apply_atomic(
            candidate_world,
            system_commands.value(),
            maximum_system_commands_per_tick_);
        if (!system_result.has_value()) {
            return core::Result<
                RuntimeSessionAdvance,
                RuntimeSessionError>::failure(
                {
                    RuntimeSessionErrorCode::system_command_batch_failed,
                    std::nullopt,
                    system_result.error(),
                    std::nullopt,
                    std::nullopt,
                    system_result.error().message
                });
        }
        command_count += system_commands.value().size();
        events.insert(
            events.end(),
            system_result.value().events.begin(),
            system_result.value().events.end());
    }

    const auto journal_result = event_journal_.append(events);
    if (!journal_result.has_value()) {
        return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::event_journal_failed,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                journal_result.error(),
                journal_result.error().message
            });
    }

    world_ = std::move(candidate_world);
    clock_ = std::move(candidate_clock);
    pending_commands_.clear();
    return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::success(
        {
            timing.value(),
            command_count,
            std::move(events)
        });
}

void RuntimeSession::clear_pending_commands() noexcept {
    pending_commands_.clear();
}

std::vector<SequencedRuntimeEvent> RuntimeSession::drain_events() {
    return event_journal_.drain();
}

const RuntimeWorld& RuntimeSession::world() const noexcept {
    return world_;
}

std::span<const SequencedRuntimeEvent>
RuntimeSession::retained_events() const noexcept {
    return event_journal_.events();
}

std::size_t RuntimeSession::pending_command_count() const noexcept {
    return pending_commands_.size();
}

std::uint64_t RuntimeSession::completed_ticks() const noexcept {
    return clock_.completed_ticks();
}

std::chrono::nanoseconds RuntimeSession::clock_remainder() const noexcept {
    return clock_.remainder();
}

}

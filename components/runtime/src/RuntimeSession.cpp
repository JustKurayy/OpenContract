#include <contract/runtime/RuntimeSession.hpp>

#include <utility>

namespace contract::runtime {

RuntimeSession::RuntimeSession(
    RuntimeWorld world,
    FixedStepClock clock,
    std::size_t maximum_pending_commands)
    : world_(std::move(world)),
      clock_(std::move(clock)),
      maximum_pending_commands_(maximum_pending_commands) {}

core::Result<RuntimeSession, RuntimeSessionError> RuntimeSession::create(
    RuntimeWorld world,
    std::chrono::nanoseconds simulation_step,
    std::size_t maximum_catch_up_ticks,
    std::size_t maximum_pending_commands) {
    auto clock = FixedStepClock::create(
        simulation_step,
        maximum_catch_up_ticks);
    if (!clock.has_value()) {
        return core::Result<RuntimeSession, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::invalid_clock,
                clock.error(),
                std::nullopt,
                clock.error().message
            });
    }
    return core::Result<RuntimeSession, RuntimeSessionError>::success(
        RuntimeSession(
            std::move(world),
            std::move(clock.value()),
            maximum_pending_commands));
}

core::Result<void, RuntimeSessionError> RuntimeSession::enqueue(
    RuntimeCommand command) {
    if (pending_commands_.size() >= maximum_pending_commands_) {
        return core::Result<void, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::pending_command_limit_exceeded,
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
    auto candidate_clock = clock_;
    auto timing = candidate_clock.advance(elapsed);
    if (!timing.has_value()) {
        return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::clock_advance_failed,
                timing.error(),
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
    const auto applied = command_processor_.apply_atomic(
        candidate_world,
        pending_commands_,
        maximum_pending_commands_);
    if (!applied.has_value()) {
        return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::failure(
            {
                RuntimeSessionErrorCode::command_batch_failed,
                std::nullopt,
                applied.error(),
                applied.error().message
            });
    }

    const auto command_count = pending_commands_.size();
    world_ = std::move(candidate_world);
    clock_ = std::move(candidate_clock);
    pending_commands_.clear();
    return core::Result<RuntimeSessionAdvance, RuntimeSessionError>::success(
        {
            timing.value(),
            command_count,
            std::move(applied.value().events)
        });
}

void RuntimeSession::clear_pending_commands() noexcept {
    pending_commands_.clear();
}

const RuntimeWorld& RuntimeSession::world() const noexcept {
    return world_;
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

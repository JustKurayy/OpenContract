#include "TestSupport.hpp"

#include <contract/runtime/RuntimeSession.hpp>
#include <contract/runtime/RuntimeSystem.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

contract::runtime::RuntimeWorld make_world() {
    using namespace contract;

    const scene::EntityDefinition entity{
        scene::EntityId("entity.synthetic"),
        {},
        {}};
    const mission::MissionObjective objective{
        mission::ObjectiveId("objective.synthetic"),
        {entity.id},
        {}};
    return runtime::RuntimeWorld(
        mission::MissionId("mission.synthetic"),
        scene::MapId("map.synthetic"),
        std::nullopt,
        {{entity, true}},
        {{objective, runtime::ObjectiveProgress::pending}});
}

class CompleteWhenDisabledSystem final
    : public contract::runtime::RuntimeSystem {
public:
    [[nodiscard]] contract::core::Result<
        void,
        contract::runtime::RuntimeSystemFailure>
    evaluate(
        const contract::runtime::RuntimeWorld& world,
        const contract::runtime::RuntimeSystemContext& context,
        contract::runtime::RuntimeCommandEmitter& emitter) const override {
        observed_ticks_.push_back(context.tick);
        if (!world.entities()[0].enabled &&
            world.objectives()[0].progress ==
                contract::runtime::ObjectiveProgress::pending) {
            return emitter.emit(
                contract::runtime::CompleteObjectiveCommand{
                    contract::mission::ObjectiveId("objective.synthetic")});
        }
        return contract::core::Result<
            void,
            contract::runtime::RuntimeSystemFailure>::success();
    }

    [[nodiscard]] const std::vector<std::uint64_t>&
    observed_ticks() const noexcept {
        return observed_ticks_;
    }

private:
    mutable std::vector<std::uint64_t> observed_ticks_;
};

class ExcessiveCommandSystem final
    : public contract::runtime::RuntimeSystem {
public:
    [[nodiscard]] contract::core::Result<
        void,
        contract::runtime::RuntimeSystemFailure>
    evaluate(
        const contract::runtime::RuntimeWorld&,
        const contract::runtime::RuntimeSystemContext&,
        contract::runtime::RuntimeCommandEmitter& emitter) const override {
        const auto first = emitter.emit(
            contract::runtime::SetEntityEnabledCommand{
                contract::scene::EntityId("entity.synthetic"),
                false});
        if (!first.has_value()) {
            return first;
        }
        return emitter.emit(
            contract::runtime::SetEntityEnabledCommand{
                contract::scene::EntityId("entity.synthetic"),
                true});
    }
};

class InvalidCommandSystem final
    : public contract::runtime::RuntimeSystem {
public:
    [[nodiscard]] contract::core::Result<
        void,
        contract::runtime::RuntimeSystemFailure>
    evaluate(
        const contract::runtime::RuntimeWorld&,
        const contract::runtime::RuntimeSystemContext&,
        contract::runtime::RuntimeCommandEmitter& emitter) const override {
        return emitter.emit(
            contract::runtime::CompleteObjectiveCommand{
                contract::mission::ObjectiveId("objective.missing")});
    }
};

}

int main() {
    using namespace std::chrono_literals;
    using namespace contract;

    auto created = runtime::RuntimeSession::create(
        make_world(),
        10ms,
        std::size_t{2},
        std::size_t{2});
    CONTRACT_EXPECT(created.has_value());
    auto session = created.value();

    CONTRACT_EXPECT(session.enqueue(
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false}).has_value());
    CONTRACT_EXPECT(session.enqueue(
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.synthetic")}).has_value());
    const auto over_limit = session.enqueue(
        runtime::FailObjectiveCommand{
            mission::ObjectiveId("objective.synthetic")});
    CONTRACT_EXPECT(!over_limit.has_value());
    CONTRACT_EXPECT_EQ(
        over_limit.error().code,
        runtime::RuntimeSessionErrorCode::pending_command_limit_exceeded);
    CONTRACT_EXPECT_EQ(session.pending_command_count(), std::size_t{2});

    const auto partial = session.advance(5ms);
    CONTRACT_EXPECT(partial.has_value());
    CONTRACT_EXPECT_EQ(partial.value().timing.tick_count, std::size_t{0});
    CONTRACT_EXPECT_EQ(partial.value().commands_applied, std::size_t{0});
    CONTRACT_EXPECT(partial.value().events.empty());
    CONTRACT_EXPECT_EQ(session.pending_command_count(), std::size_t{2});
    CONTRACT_EXPECT(session.world().entities()[0].enabled);

    const auto tick = session.advance(5ms);
    CONTRACT_EXPECT(tick.has_value());
    CONTRACT_EXPECT_EQ(tick.value().timing.tick_count, std::size_t{1});
    CONTRACT_EXPECT_EQ(tick.value().commands_applied, std::size_t{2});
    CONTRACT_EXPECT_EQ(tick.value().events.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(session.pending_command_count(), std::size_t{0});
    CONTRACT_EXPECT(!session.world().entities()[0].enabled);
    CONTRACT_EXPECT(session.world().all_objectives_complete());
    CONTRACT_EXPECT_EQ(session.completed_ticks(), std::uint64_t{1});
    CONTRACT_EXPECT_EQ(session.retained_events().size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(
        session.retained_events()[0].sequence,
        std::uint64_t{0});
    CONTRACT_EXPECT_EQ(
        session.retained_events()[1].sequence,
        std::uint64_t{1});
    const auto drained_events = session.drain_events();
    CONTRACT_EXPECT_EQ(drained_events.size(), std::size_t{2});
    CONTRACT_EXPECT(session.retained_events().empty());

    auto rollback_session_result = runtime::RuntimeSession::create(
        make_world(),
        10ms,
        std::size_t{2},
        std::size_t{2});
    CONTRACT_EXPECT(rollback_session_result.has_value());
    auto rollback_session = rollback_session_result.value();
    CONTRACT_EXPECT(rollback_session.enqueue(
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.missing")}).has_value());

    const auto failed_tick = rollback_session.advance(10ms);
    CONTRACT_EXPECT(!failed_tick.has_value());
    CONTRACT_EXPECT_EQ(
        failed_tick.error().code,
        runtime::RuntimeSessionErrorCode::command_batch_failed);
    CONTRACT_EXPECT(failed_tick.error().command_error.has_value());
    CONTRACT_EXPECT_EQ(rollback_session.completed_ticks(), std::uint64_t{0});
    CONTRACT_EXPECT_EQ(rollback_session.clock_remainder(), 0ns);
    CONTRACT_EXPECT_EQ(rollback_session.pending_command_count(), std::size_t{1});
    CONTRACT_EXPECT_EQ(
        rollback_session.world().objectives()[0].progress,
        runtime::ObjectiveProgress::pending);

    rollback_session.clear_pending_commands();
    CONTRACT_EXPECT_EQ(rollback_session.pending_command_count(), std::size_t{0});
    const auto recovered = rollback_session.advance(10ms);
    CONTRACT_EXPECT(recovered.has_value());
    CONTRACT_EXPECT_EQ(recovered.value().timing.tick_count, std::size_t{1});
    CONTRACT_EXPECT_EQ(rollback_session.completed_ticks(), std::uint64_t{1});

    const auto invalid_clock = runtime::RuntimeSession::create(
        make_world(),
        0ns,
        std::size_t{2},
        std::size_t{2});
    CONTRACT_EXPECT(!invalid_clock.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_clock.error().code,
        runtime::RuntimeSessionErrorCode::invalid_clock);

    auto event_limited_result = runtime::RuntimeSession::create(
        make_world(),
        10ms,
        std::size_t{2},
        std::size_t{2},
        std::size_t{1});
    CONTRACT_EXPECT(event_limited_result.has_value());
    auto event_limited = event_limited_result.value();
    CONTRACT_EXPECT(event_limited.enqueue(
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false}).has_value());
    CONTRACT_EXPECT(event_limited.enqueue(
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.synthetic")}).has_value());

    const auto journal_failure = event_limited.advance(10ms);
    CONTRACT_EXPECT(!journal_failure.has_value());
    CONTRACT_EXPECT_EQ(
        journal_failure.error().code,
        runtime::RuntimeSessionErrorCode::event_journal_failed);
    CONTRACT_EXPECT(journal_failure.error().event_journal_error.has_value());
    CONTRACT_EXPECT_EQ(event_limited.completed_ticks(), std::uint64_t{0});
    CONTRACT_EXPECT_EQ(
        event_limited.pending_command_count(),
        std::size_t{2});
    CONTRACT_EXPECT(event_limited.world().entities()[0].enabled);
    CONTRACT_EXPECT_EQ(
        event_limited.world().objectives()[0].progress,
        runtime::ObjectiveProgress::pending);
    CONTRACT_EXPECT(event_limited.retained_events().empty());

    auto system_session_result = runtime::RuntimeSession::create(
        make_world(),
        10ms,
        std::size_t{2},
        std::size_t{2},
        std::size_t{4},
        std::size_t{1});
    CONTRACT_EXPECT(system_session_result.has_value());
    auto system_session = system_session_result.value();
    CONTRACT_EXPECT(system_session.enqueue(
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false}).has_value());
    const CompleteWhenDisabledSystem completion_system;
    const runtime::RuntimeSystem* completion_systems[]{&completion_system};

    const auto system_tick = system_session.advance(20ms, completion_systems);
    CONTRACT_EXPECT(system_tick.has_value());
    CONTRACT_EXPECT_EQ(system_tick.value().timing.tick_count, std::size_t{2});
    CONTRACT_EXPECT_EQ(system_tick.value().commands_applied, std::size_t{2});
    CONTRACT_EXPECT_EQ(system_tick.value().events.size(), std::size_t{2});
    CONTRACT_EXPECT(!system_session.world().entities()[0].enabled);
    CONTRACT_EXPECT(system_session.world().all_objectives_complete());
    CONTRACT_EXPECT_EQ(system_session.completed_ticks(), std::uint64_t{2});
    CONTRACT_EXPECT_EQ(
        completion_system.observed_ticks().size(),
        std::size_t{2});
    CONTRACT_EXPECT_EQ(
        completion_system.observed_ticks()[0],
        std::uint64_t{0});
    CONTRACT_EXPECT_EQ(
        completion_system.observed_ticks()[1],
        std::uint64_t{1});

    auto excessive_session_result = runtime::RuntimeSession::create(
        make_world(),
        10ms,
        std::size_t{2},
        std::size_t{2},
        std::size_t{4},
        std::size_t{1});
    CONTRACT_EXPECT(excessive_session_result.has_value());
    auto excessive_session = excessive_session_result.value();
    CONTRACT_EXPECT(excessive_session.enqueue(
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false}).has_value());
    const ExcessiveCommandSystem excessive_system;
    const runtime::RuntimeSystem* excessive_systems[]{&excessive_system};
    const auto excessive_tick =
        excessive_session.advance(10ms, excessive_systems);
    CONTRACT_EXPECT(!excessive_tick.has_value());
    CONTRACT_EXPECT_EQ(
        excessive_tick.error().code,
        runtime::RuntimeSessionErrorCode::system_evaluation_failed);
    CONTRACT_EXPECT(excessive_tick.error().system_error.has_value());
    CONTRACT_EXPECT_EQ(excessive_session.completed_ticks(), std::uint64_t{0});
    CONTRACT_EXPECT_EQ(
        excessive_session.pending_command_count(),
        std::size_t{1});
    CONTRACT_EXPECT(excessive_session.world().entities()[0].enabled);
    CONTRACT_EXPECT(excessive_session.retained_events().empty());

    auto invalid_system_session_result = runtime::RuntimeSession::create(
        make_world(),
        10ms,
        std::size_t{2},
        std::size_t{2},
        std::size_t{4},
        std::size_t{1});
    CONTRACT_EXPECT(invalid_system_session_result.has_value());
    auto invalid_system_session = invalid_system_session_result.value();
    const InvalidCommandSystem invalid_command_system;
    const runtime::RuntimeSystem* invalid_command_systems[]{
        &invalid_command_system};
    const auto invalid_system_tick =
        invalid_system_session.advance(10ms, invalid_command_systems);
    CONTRACT_EXPECT(!invalid_system_tick.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_system_tick.error().code,
        runtime::RuntimeSessionErrorCode::system_command_batch_failed);
    CONTRACT_EXPECT(invalid_system_tick.error().command_error.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_system_session.completed_ticks(),
        std::uint64_t{0});
    CONTRACT_EXPECT_EQ(
        invalid_system_session.world().objectives()[0].progress,
        runtime::ObjectiveProgress::pending);
    CONTRACT_EXPECT(invalid_system_session.retained_events().empty());

    return test::finish();
}

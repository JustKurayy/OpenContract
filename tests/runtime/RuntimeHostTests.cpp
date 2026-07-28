#include "TestSupport.hpp"

#include <contract/runtime/RuntimeHost.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
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

contract::runtime::RuntimeSession make_session(
    std::size_t maximum_pending_commands = 2) {
    using namespace std::chrono_literals;
    auto created = contract::runtime::RuntimeSession::create(
        make_world(),
        10ms,
        std::size_t{2},
        maximum_pending_commands);
    return std::move(created.value());
}

class CompleteWhenDisabledSystem final
    : public contract::runtime::RuntimeSystem {
public:
    [[nodiscard]] contract::core::Result<
        void,
        contract::runtime::RuntimeSystemFailure>
    evaluate(
        const contract::runtime::RuntimeWorld& world,
        const contract::runtime::RuntimeSystemContext&,
        contract::runtime::RuntimeCommandEmitter& emitter) const override {
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
};

}

int main() {
    using namespace std::chrono_literals;
    using namespace contract;

    std::vector<std::unique_ptr<runtime::RuntimeSystem>> systems;
    systems.push_back(std::make_unique<CompleteWhenDisabledSystem>());
    auto created = runtime::RuntimeHost::create(
        make_session(),
        std::move(systems));
    CONTRACT_EXPECT(created.has_value());
    auto host = std::move(created.value());

    const runtime::RuntimeCommand disable_commands[]{
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false}
    };
    const auto frame = host.advance(10ms, disable_commands);
    CONTRACT_EXPECT(frame.has_value());
    CONTRACT_EXPECT_EQ(
        frame.value().advance.timing.tick_count,
        std::size_t{1});
    CONTRACT_EXPECT_EQ(
        frame.value().advance.commands_applied,
        std::size_t{2});
    CONTRACT_EXPECT_EQ(
        frame.value().advance.events.size(),
        std::size_t{2});
    CONTRACT_EXPECT_EQ(
        frame.value().observation.completed_ticks,
        std::uint64_t{1});
    CONTRACT_EXPECT(!frame.value().observation.entities[0].enabled);
    CONTRACT_EXPECT(
        frame.value().observation.all_objectives_complete);
    CONTRACT_EXPECT_EQ(
        frame.value().observation.next_event_sequence,
        std::uint64_t{2});
    CONTRACT_EXPECT_EQ(
        frame.value().observation.retained_events.size(),
        std::size_t{2});
    CONTRACT_EXPECT_EQ(
        host.observe().completed_ticks,
        std::uint64_t{1});

    auto limited_created = runtime::RuntimeHost::create(
        make_session(std::size_t{1}),
        {});
    CONTRACT_EXPECT(limited_created.has_value());
    auto limited_host = std::move(limited_created.value());
    const runtime::RuntimeCommand excessive_commands[]{
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false},
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.synthetic")}
    };
    const auto rejected = limited_host.advance(
        10ms,
        excessive_commands);
    CONTRACT_EXPECT(!rejected.has_value());
    CONTRACT_EXPECT_EQ(
        rejected.error().code,
        runtime::RuntimeHostErrorCode::command_enqueue_failed);
    CONTRACT_EXPECT_EQ(rejected.error().command_index, std::size_t{1});
    CONTRACT_EXPECT(rejected.error().session_error.has_value());
    CONTRACT_EXPECT_EQ(
        limited_host.observe().completed_ticks,
        std::uint64_t{0});
    CONTRACT_EXPECT(limited_host.observe().entities[0].enabled);

    const auto empty_frame = limited_host.advance(10ms, {});
    CONTRACT_EXPECT(empty_frame.has_value());
    CONTRACT_EXPECT_EQ(
        empty_frame.value().advance.commands_applied,
        std::size_t{0});
    CONTRACT_EXPECT(empty_frame.value().observation.entities[0].enabled);

    auto partial_created = runtime::RuntimeHost::create(
        make_session(),
        {});
    CONTRACT_EXPECT(partial_created.has_value());
    auto partial_host = std::move(partial_created.value());
    const auto partial = partial_host.advance(5ms, disable_commands);
    CONTRACT_EXPECT(partial.has_value());
    CONTRACT_EXPECT_EQ(
        partial.value().advance.timing.tick_count,
        std::size_t{0});
    CONTRACT_EXPECT(partial.value().observation.entities[0].enabled);
    CONTRACT_EXPECT_EQ(
        partial.value().observation.clock_remainder,
        5ms);
    const auto completed = partial_host.advance(5ms, {});
    CONTRACT_EXPECT(completed.has_value());
    CONTRACT_EXPECT_EQ(
        completed.value().advance.commands_applied,
        std::size_t{1});
    CONTRACT_EXPECT(!completed.value().observation.entities[0].enabled);

    const auto negative = partial_host.advance(-1ns, {});
    CONTRACT_EXPECT(!negative.has_value());
    CONTRACT_EXPECT_EQ(
        negative.error().code,
        runtime::RuntimeHostErrorCode::session_advance_failed);
    CONTRACT_EXPECT(negative.error().session_error.has_value());
    CONTRACT_EXPECT_EQ(
        partial_host.observe().completed_ticks,
        std::uint64_t{1});

    std::vector<std::unique_ptr<runtime::RuntimeSystem>> null_systems;
    null_systems.push_back(nullptr);
    const auto null_system = runtime::RuntimeHost::create(
        make_session(),
        std::move(null_systems));
    CONTRACT_EXPECT(!null_system.has_value());
    CONTRACT_EXPECT_EQ(
        null_system.error().code,
        runtime::RuntimeHostErrorCode::null_system);
    CONTRACT_EXPECT_EQ(
        null_system.error().system_index,
        std::size_t{0});

    return test::finish();
}

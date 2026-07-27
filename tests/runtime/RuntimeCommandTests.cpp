#include "TestSupport.hpp"

#include <contract/runtime/RuntimeCommand.hpp>

#include <contract/mission/Mission.hpp>
#include <contract/scene/Scene.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
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

}

int main() {
    using namespace contract;

    runtime::RuntimeCommandProcessor processor;
    auto world = make_world();

    scene::Transform moved;
    moved.position = {3.0F, 2.0F, 1.0F};
    const std::vector<runtime::RuntimeCommand> valid_commands{
        runtime::SetEntityTransformCommand{
            scene::EntityId("entity.synthetic"),
            moved},
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false},
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.synthetic")}};

    const auto applied = processor.apply_atomic(world, valid_commands);
    CONTRACT_EXPECT(applied.has_value());
    CONTRACT_EXPECT_EQ(applied.value().events.size(), std::size_t{3});
    CONTRACT_EXPECT(std::holds_alternative<
        runtime::EntityTransformChangedEvent>(applied.value().events[0]));
    CONTRACT_EXPECT(std::holds_alternative<
        runtime::EntityEnabledChangedEvent>(applied.value().events[1]));
    CONTRACT_EXPECT(std::holds_alternative<
        runtime::ObjectiveProgressChangedEvent>(applied.value().events[2]));
    const auto& enabled_event = std::get<
        runtime::EntityEnabledChangedEvent>(applied.value().events[1]);
    CONTRACT_EXPECT_EQ(
        enabled_event.entity.value(),
        std::string("entity.synthetic"));
    CONTRACT_EXPECT(!enabled_event.enabled);
    const auto& objective_event = std::get<
        runtime::ObjectiveProgressChangedEvent>(applied.value().events[2]);
    CONTRACT_EXPECT_EQ(
        objective_event.progress,
        runtime::ObjectiveProgress::completed);
    CONTRACT_EXPECT_EQ(
        world.entities()[0].definition.transform.position[0],
        3.0F);
    CONTRACT_EXPECT(!world.entities()[0].enabled);
    CONTRACT_EXPECT(world.all_objectives_complete());

    auto rollback_world = make_world();
    const std::vector<runtime::RuntimeCommand> failing_commands{
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.synthetic"),
            false},
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.missing")}};
    const auto failed = processor.apply_atomic(
        rollback_world,
        failing_commands);
    CONTRACT_EXPECT(!failed.has_value());
    CONTRACT_EXPECT_EQ(
        failed.error().code,
        runtime::RuntimeCommandErrorCode::command_failed);
    CONTRACT_EXPECT_EQ(failed.error().command_index, std::size_t{1});
    CONTRACT_EXPECT(failed.error().world_error.has_value());
    CONTRACT_EXPECT_EQ(
        failed.error().world_error->code,
        runtime::RuntimeWorldErrorCode::objective_not_found);
    CONTRACT_EXPECT(rollback_world.entities()[0].enabled);
    CONTRACT_EXPECT_EQ(
        rollback_world.objectives()[0].progress,
        runtime::ObjectiveProgress::pending);

    auto transition_world = make_world();
    const std::vector<runtime::RuntimeCommand> conflicting_commands{
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.synthetic")},
        runtime::FailObjectiveCommand{
            mission::ObjectiveId("objective.synthetic")}};
    const auto conflicting = processor.apply_atomic(
        transition_world,
        conflicting_commands);
    CONTRACT_EXPECT(!conflicting.has_value());
    CONTRACT_EXPECT_EQ(conflicting.error().command_index, std::size_t{1});
    CONTRACT_EXPECT_EQ(
        transition_world.objectives()[0].progress,
        runtime::ObjectiveProgress::pending);

    auto limited_world = make_world();
    const auto limited = processor.apply_atomic(
        limited_world,
        valid_commands,
        std::size_t{2});
    CONTRACT_EXPECT(!limited.has_value());
    CONTRACT_EXPECT_EQ(
        limited.error().code,
        runtime::RuntimeCommandErrorCode::command_limit_exceeded);
    CONTRACT_EXPECT(limited_world.entities()[0].enabled);
    CONTRACT_EXPECT_EQ(
        limited_world.objectives()[0].progress,
        runtime::ObjectiveProgress::pending);

    const std::vector<runtime::RuntimeCommand> empty_commands;
    const auto empty = processor.apply_atomic(
        limited_world,
        empty_commands,
        std::size_t{0});
    CONTRACT_EXPECT(empty.has_value());
    CONTRACT_EXPECT(empty.value().events.empty());

    return test::finish();
}

#include "TestSupport.hpp"

#include <contract/runtime/RuntimeWorld.hpp>

#include <contract/modding/PackageSet.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace {

contract::modding::ModPackage package_with_world(std::string package_id) {
    using namespace contract;

    const scene::MapId map_id("map.synthetic");
    const scene::EntityId target_id("entity.target");
    const scene::EntityId spawn_id("entity.spawn");
    const scene::EntityDefinition target{
        target_id,
        {},
        {}};
    const scene::EntityDefinition spawn{
        spawn_id,
        {},
        {}};
    const mission::MissionObjective objective{
        mission::ObjectiveId("objective.synthetic"),
        {target_id},
        {spawn_id}};
    const navigation::NavigationGraphId navigation_id("navigation.synthetic");
    const navigation::NavigationGraph navigation_graph{
        navigation_id,
        {
            {
                navigation::NavigationNodeId("node.start"),
                {0.0F, 0.0F, 0.0F},
                {navigation::NavigationNodeId("node.goal")}
            },
            {
                navigation::NavigationNodeId("node.goal"),
                {1.0F, 0.0F, 0.0F},
                {}
            }
        }};
    return {
        modding::ModPackageId(std::move(package_id)),
        {1, 0, 0},
        {"Synthetic", "Synthetic Author", "Synthetic only"},
        {},
        {},
        {navigation_graph},
        {
            scene::MapDefinition{
                map_id,
                {target, spawn},
                navigation_id}
        },
        {
            mission::MissionDefinition{
                mission::MissionId("mission.synthetic"),
                map_id,
                {objective}}
        }};
}

}

int main() {
    using namespace contract;

    modding::LoadedPackageSet package_set;
    package_set.packages.push_back(package_with_world("package.synthetic"));
    package_set.load_order.push_back(
        modding::ModPackageId("package.synthetic"));

    const runtime::RuntimeWorldBuilder builder;
    auto built = builder.build(package_set, "mission.synthetic");
    CONTRACT_EXPECT(built.has_value());
    CONTRACT_EXPECT_EQ(
        built.value().mission_id().value(),
        std::string("mission.synthetic"));
    CONTRACT_EXPECT_EQ(
        built.value().map_id().value(),
        std::string("map.synthetic"));
    CONTRACT_EXPECT_EQ(built.value().entities().size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(built.value().objectives().size(), std::size_t{1});
    CONTRACT_EXPECT(built.value().navigation_graph().has_value());
    CONTRACT_EXPECT_EQ(
        built.value().navigation_graph()->id.value(),
        std::string("navigation.synthetic"));
    CONTRACT_EXPECT_EQ(
        built.value().objectives()[0].progress,
        runtime::ObjectiveProgress::pending);
    CONTRACT_EXPECT(!built.value().all_objectives_complete());

    scene::Transform moved_transform;
    moved_transform.position = {5.0F, 2.0F, 1.0F};
    const auto moved = built.value().set_entity_transform(
        scene::EntityId("entity.target"),
        moved_transform);
    CONTRACT_EXPECT(moved.has_value());
    CONTRACT_EXPECT_EQ(
        built.value().entities()[0].definition.transform.position[0],
        5.0F);

    const auto disabled = built.value().set_entity_enabled(
        scene::EntityId("entity.target"),
        false);
    CONTRACT_EXPECT(disabled.has_value());
    CONTRACT_EXPECT(!built.value().entities()[0].enabled);

    const auto missing_entity = built.value().set_entity_enabled(
        scene::EntityId("entity.missing"),
        false);
    CONTRACT_EXPECT(!missing_entity.has_value());
    CONTRACT_EXPECT_EQ(
        missing_entity.error().code,
        runtime::RuntimeWorldErrorCode::entity_not_found);

    auto invalid_transform = moved_transform;
    invalid_transform.position[0] =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalid_move = built.value().set_entity_transform(
        scene::EntityId("entity.target"),
        invalid_transform);
    CONTRACT_EXPECT(!invalid_move.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_move.error().code,
        runtime::RuntimeWorldErrorCode::invalid_transform);
    CONTRACT_EXPECT_EQ(
        built.value().entities()[0].definition.transform.position[0],
        5.0F);

    const auto completed = built.value().complete_objective(
        mission::ObjectiveId("objective.synthetic"));
    CONTRACT_EXPECT(completed.has_value());
    CONTRACT_EXPECT(built.value().all_objectives_complete());
    CONTRACT_EXPECT_EQ(
        built.value().objectives()[0].progress,
        runtime::ObjectiveProgress::completed);

    const auto repeated = built.value().complete_objective(
        mission::ObjectiveId("objective.synthetic"));
    CONTRACT_EXPECT(!repeated.has_value());
    CONTRACT_EXPECT_EQ(
        repeated.error().code,
        runtime::RuntimeWorldErrorCode::invalid_objective_transition);

    const auto missing_objective = built.value().complete_objective(
        mission::ObjectiveId("objective.missing"));
    CONTRACT_EXPECT(!missing_objective.has_value());
    CONTRACT_EXPECT_EQ(
        missing_objective.error().code,
        runtime::RuntimeWorldErrorCode::objective_not_found);

    auto failed_world = builder.build(package_set, "mission.synthetic");
    CONTRACT_EXPECT(failed_world.has_value());
    const auto failed = failed_world.value().fail_objective(
        mission::ObjectiveId("objective.synthetic"));
    CONTRACT_EXPECT(failed.has_value());
    CONTRACT_EXPECT_EQ(
        failed_world.value().objectives()[0].progress,
        runtime::ObjectiveProgress::failed);
    CONTRACT_EXPECT(!failed_world.value().all_objectives_complete());
    const auto complete_failed = failed_world.value().complete_objective(
        mission::ObjectiveId("objective.synthetic"));
    CONTRACT_EXPECT(!complete_failed.has_value());
    CONTRACT_EXPECT_EQ(
        complete_failed.error().code,
        runtime::RuntimeWorldErrorCode::invalid_objective_transition);

    const auto missing_mission = builder.build(package_set, "mission.missing");
    CONTRACT_EXPECT(!missing_mission.has_value());
    CONTRACT_EXPECT_EQ(
        missing_mission.error().code,
        runtime::RuntimeWorldErrorCode::mission_not_found);

    auto ambiguous_set = package_set;
    ambiguous_set.packages.push_back(package_with_world("package.second"));
    ambiguous_set.load_order.push_back(
        modding::ModPackageId("package.second"));
    const auto ambiguous = builder.build(ambiguous_set, "mission.synthetic");
    CONTRACT_EXPECT(!ambiguous.has_value());
    CONTRACT_EXPECT_EQ(
        ambiguous.error().code,
        runtime::RuntimeWorldErrorCode::mission_ambiguous);

    auto missing_map_set = package_set;
    missing_map_set.packages[0].maps.clear();
    const auto missing_map = builder.build(
        missing_map_set,
        "mission.synthetic");
    CONTRACT_EXPECT(!missing_map.has_value());
    CONTRACT_EXPECT_EQ(
        missing_map.error().code,
        runtime::RuntimeWorldErrorCode::map_not_found);

    auto missing_navigation_set = package_set;
    missing_navigation_set.packages[0].navigation_graphs.clear();
    const auto missing_navigation = builder.build(
        missing_navigation_set,
        "mission.synthetic");
    CONTRACT_EXPECT(!missing_navigation.has_value());
    CONTRACT_EXPECT_EQ(
        missing_navigation.error().code,
        runtime::RuntimeWorldErrorCode::navigation_not_found);

    return test::finish();
}

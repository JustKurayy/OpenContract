#include "TestSupport.hpp"

#include <contract/runtime/RuntimeObservation.hpp>
#include <contract/runtime/RuntimeSession.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace {

contract::runtime::RuntimeWorld make_world() {
    using namespace contract;

    scene::Transform second_transform;
    second_transform.position = {1.0F, 2.0F, 3.0F};
    const scene::EntityDefinition second_entity{
        scene::EntityId("entity.second"),
        second_transform,
        {
            scene::ComponentReference{
                "synthetic.visual",
                {assets::AssetId("asset.synthetic")}}
        }};
    const scene::EntityDefinition first_entity{
        scene::EntityId("entity.first"),
        {},
        {}};
    const mission::MissionObjective second_objective{
        mission::ObjectiveId("objective.second"),
        {second_entity.id},
        {first_entity.id}};
    const mission::MissionObjective first_objective{
        mission::ObjectiveId("objective.first"),
        {first_entity.id},
        {}};
    const navigation::NavigationNodeId second_node_id("node.second");
    const navigation::NavigationNodeId first_node_id("node.first");
    const navigation::NavigationGraph navigation_graph{
        navigation::NavigationGraphId("navigation.synthetic"),
        {
            navigation::NavigationNode{
                second_node_id,
                {4.0F, 5.0F, 6.0F},
                {first_node_id}},
            navigation::NavigationNode{
                first_node_id,
                {0.0F, 0.0F, 0.0F},
                {}}
        }};

    return runtime::RuntimeWorld(
        mission::MissionId("mission.synthetic"),
        scene::MapId("map.synthetic"),
        navigation_graph,
        {
            {second_entity, true},
            {first_entity, false}
        },
        {
            {second_objective, runtime::ObjectiveProgress::pending},
            {first_objective, runtime::ObjectiveProgress::pending}
        });
}

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

    const auto initial = session.observe();
    CONTRACT_EXPECT_EQ(
        initial.mission,
        mission::MissionId("mission.synthetic"));
    CONTRACT_EXPECT_EQ(initial.map, scene::MapId("map.synthetic"));
    CONTRACT_EXPECT_EQ(initial.completed_ticks, std::uint64_t{0});
    CONTRACT_EXPECT_EQ(initial.simulation_step, 10ms);
    CONTRACT_EXPECT_EQ(initial.clock_remainder, 0ns);
    CONTRACT_EXPECT(initial.navigation_graph.has_value());
    CONTRACT_EXPECT_EQ(
        initial.navigation_graph->id,
        navigation::NavigationGraphId("navigation.synthetic"));
    CONTRACT_EXPECT_EQ(
        initial.navigation_graph->nodes.size(),
        std::size_t{2});
    CONTRACT_EXPECT_EQ(
        initial.navigation_graph->nodes[0].id,
        navigation::NavigationNodeId("node.second"));
    CONTRACT_EXPECT_EQ(
        initial.navigation_graph->nodes[0].neighbors[0],
        navigation::NavigationNodeId("node.first"));
    CONTRACT_EXPECT_EQ(initial.next_event_sequence, std::uint64_t{0});
    CONTRACT_EXPECT(initial.retained_events.empty());
    CONTRACT_EXPECT_EQ(initial.entities.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(
        initial.entities[0].id,
        scene::EntityId("entity.second"));
    CONTRACT_EXPECT_EQ(
        initial.entities[1].id,
        scene::EntityId("entity.first"));
    CONTRACT_EXPECT_EQ(
        initial.entities[0].transform.position[0],
        1.0F);
    CONTRACT_EXPECT_EQ(
        initial.entities[0].components[0].type,
        std::string("synthetic.visual"));
    CONTRACT_EXPECT(initial.entities[0].enabled);
    CONTRACT_EXPECT(!initial.entities[1].enabled);
    CONTRACT_EXPECT_EQ(initial.objectives.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(
        initial.objectives[0].id,
        mission::ObjectiveId("objective.second"));
    CONTRACT_EXPECT_EQ(
        initial.objectives[1].id,
        mission::ObjectiveId("objective.first"));
    CONTRACT_EXPECT_EQ(
        initial.objectives[0].target_references[0],
        scene::EntityId("entity.second"));
    CONTRACT_EXPECT_EQ(
        initial.objectives[0].spawn_references[0],
        scene::EntityId("entity.first"));
    CONTRACT_EXPECT_EQ(
        initial.objectives[0].progress,
        runtime::ObjectiveProgress::pending);
    CONTRACT_EXPECT(!initial.all_objectives_complete);

    CONTRACT_EXPECT(session.enqueue(
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.second"),
            false}).has_value());
    CONTRACT_EXPECT(session.enqueue(
        runtime::CompleteObjectiveCommand{
            mission::ObjectiveId("objective.second")}).has_value());
    const auto advance = session.advance(15ms);
    CONTRACT_EXPECT(advance.has_value());

    auto observed = session.observe();
    CONTRACT_EXPECT_EQ(observed.completed_ticks, std::uint64_t{1});
    CONTRACT_EXPECT_EQ(observed.simulation_step, 10ms);
    CONTRACT_EXPECT_EQ(observed.clock_remainder, 5ms);
    CONTRACT_EXPECT_EQ(observed.next_event_sequence, std::uint64_t{2});
    CONTRACT_EXPECT_EQ(observed.retained_events.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(
        observed.retained_events[0].sequence,
        std::uint64_t{0});
    CONTRACT_EXPECT_EQ(
        observed.retained_events[1].sequence,
        std::uint64_t{1});
    CONTRACT_EXPECT(
        std::holds_alternative<runtime::EntityEnabledChangedEvent>(
            observed.retained_events[0].event));
    CONTRACT_EXPECT(
        std::holds_alternative<runtime::ObjectiveProgressChangedEvent>(
            observed.retained_events[1].event));
    CONTRACT_EXPECT(!observed.entities[0].enabled);
    CONTRACT_EXPECT_EQ(
        observed.objectives[0].progress,
        runtime::ObjectiveProgress::completed);
    CONTRACT_EXPECT(!observed.all_objectives_complete);

    observed.entities[0].transform.position[0] = 99.0F;
    observed.entities[0].components[0].type = "mutated";
    observed.navigation_graph->nodes[0].position[0] = 99.0F;
    observed.navigation_graph->nodes[0].neighbors.clear();
    observed.objectives[0].progress = runtime::ObjectiveProgress::failed;
    observed.retained_events.clear();

    const auto unchanged = session.observe();
    CONTRACT_EXPECT_EQ(
        unchanged.entities[0].transform.position[0],
        1.0F);
    CONTRACT_EXPECT_EQ(
        unchanged.entities[0].components[0].type,
        std::string("synthetic.visual"));
    CONTRACT_EXPECT_EQ(
        unchanged.navigation_graph->nodes[0].position[0],
        4.0F);
    CONTRACT_EXPECT_EQ(
        unchanged.navigation_graph->nodes[0].neighbors.size(),
        std::size_t{1});
    CONTRACT_EXPECT_EQ(
        unchanged.objectives[0].progress,
        runtime::ObjectiveProgress::completed);
    CONTRACT_EXPECT_EQ(
        unchanged.retained_events.size(),
        std::size_t{2});

    const auto drained = session.drain_events();
    CONTRACT_EXPECT_EQ(drained.size(), std::size_t{2});
    const auto after_drain = session.observe();
    CONTRACT_EXPECT(after_drain.retained_events.empty());
    CONTRACT_EXPECT_EQ(
        after_drain.next_event_sequence,
        std::uint64_t{2});

    CONTRACT_EXPECT(session.enqueue(
        runtime::SetEntityEnabledCommand{
            scene::EntityId("entity.second"),
            true}).has_value());
    const auto next_tick = session.advance(5ms);
    CONTRACT_EXPECT(next_tick.has_value());
    const auto latest = session.observe();
    CONTRACT_EXPECT_EQ(latest.completed_ticks, std::uint64_t{2});
    CONTRACT_EXPECT_EQ(latest.clock_remainder, 0ns);
    CONTRACT_EXPECT_EQ(latest.next_event_sequence, std::uint64_t{3});
    CONTRACT_EXPECT_EQ(latest.retained_events.size(), std::size_t{1});
    CONTRACT_EXPECT_EQ(
        latest.retained_events[0].sequence,
        std::uint64_t{2});
    CONTRACT_EXPECT(latest.entities[0].enabled);
    CONTRACT_EXPECT_EQ(initial.completed_ticks, std::uint64_t{0});
    CONTRACT_EXPECT(initial.entities[0].enabled);
    CONTRACT_EXPECT(initial.retained_events.empty());

    return test::finish();
}

#include "TestSupport.hpp"

#include <contract/collision/StaticCollisionWorld.hpp>
#include <contract/runtime/ExplorationObjectiveSystem.hpp>
#include <contract/runtime/PlayerController.hpp>
#include <contract/runtime/RuntimeSession.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <optional>

namespace {

bool near(float left, float right) {
    return std::fabs(left - right) < 0.001F;
}

contract::runtime::RuntimeObservation observation_with_player() {
    using namespace contract;

    runtime::RuntimeObservation observation;
    observation.mission = mission::MissionId("mission.synthetic");
    observation.map = scene::MapId("map.synthetic");
    observation.entities.push_back(
        {
            scene::EntityId("player.synthetic"),
            scene::Transform{{10.0F, 20.0F, 30.0F}},
            {
                scene::ComponentReference{
                    std::string(runtime::player_component_type),
                    {}}
            },
            true
        });
    return observation;
}

contract::runtime::RuntimeWorld exploration_world() {
    using namespace contract;

    const scene::EntityDefinition player{
        scene::EntityId("player.synthetic"),
        {},
        {
            scene::ComponentReference{
                std::string(runtime::player_component_type),
                {}}
        }};
    const mission::MissionObjective objective{
        mission::ObjectiveId("objective.explore"),
        {},
        {player.id}};
    return runtime::RuntimeWorld(
        mission::MissionId("mission.synthetic"),
        scene::MapId("map.synthetic"),
        std::nullopt,
        {{player, true}},
        {{objective, runtime::ObjectiveProgress::pending}});
}

contract::scene::CollisionScene grounding_scene() {
    contract::scene::CollisionScene scene;
    scene.vertices = {
        {-100.0F, 5.0F, -100.0F},
        {100.0F, 5.0F, -100.0F},
        {100.0F, 5.0F, 100.0F},
        {-100.0F, 5.0F, 100.0F}
    };
    scene.indices = {0, 2, 1, 0, 3, 2};
    return scene;
}

}

int main() {
    using namespace std::chrono_literals;
    using namespace contract;

    const runtime::PlayerController controller(
        scene::EntityId("player.synthetic"),
        10.0F,
        2.0F);
    runtime::PlayerInput diagonal;
    diagonal.forward = 1.0F;
    diagonal.right = 1.0F;
    const auto moved = controller.update(
        observation_with_player(),
        diagonal,
        1.0F);
    CONTRACT_EXPECT(moved.has_value());
    CONTRACT_EXPECT(moved.value().has_value());
    if (moved.has_value() && moved.value().has_value()) {
        const auto& command =
            std::get<runtime::SetEntityTransformCommand>(
                moved.value().value());
        CONTRACT_EXPECT(near(
            command.transform.position[0],
            10.0F + std::sqrt(50.0F)));
        CONTRACT_EXPECT_EQ(command.transform.position[1], 20.0F);
        CONTRACT_EXPECT(near(
            command.transform.position[2],
            30.0F + std::sqrt(50.0F)));
        CONTRACT_EXPECT(near(
            command.transform.rotation[1],
            std::sin(0.785398163F * 0.5F)));
        CONTRACT_EXPECT(near(
            command.transform.rotation[3],
            std::cos(0.785398163F * 0.5F)));
    }

    runtime::PlayerInput idle;
    const auto unchanged = controller.update(
        observation_with_player(),
        idle,
        1.0F);
    CONTRACT_EXPECT(unchanged.has_value());
    CONTRACT_EXPECT(!unchanged.value().has_value());

    runtime::PlayerInput sprint;
    sprint.forward = 1.0F;
    sprint.sprint = true;
    const auto sprinted = controller.update(
        observation_with_player(),
        sprint,
        0.5F);
    CONTRACT_EXPECT(sprinted.has_value());
    if (sprinted.has_value() && sprinted.value().has_value()) {
        const auto& command =
            std::get<runtime::SetEntityTransformCommand>(
                sprinted.value().value());
        CONTRACT_EXPECT_EQ(command.transform.position[2], 40.0F);
    }

    const auto invalid_step = controller.update(
        observation_with_player(),
        diagonal,
        -1.0F);
    CONTRACT_EXPECT(!invalid_step.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_step.error().code,
        runtime::PlayerControllerErrorCode::invalid_elapsed_time);

    runtime::PlayerInput invalid_input;
    invalid_input.forward =
        std::numeric_limits<float>::quiet_NaN();
    const auto rejected_input = controller.update(
        observation_with_player(),
        invalid_input,
        1.0F);
    CONTRACT_EXPECT(!rejected_input.has_value());
    CONTRACT_EXPECT_EQ(
        rejected_input.error().code,
        runtime::PlayerControllerErrorCode::invalid_input);

    const runtime::PlayerController missing_controller(
        scene::EntityId("player.missing"));
    const auto missing = missing_controller.update(
        observation_with_player(),
        diagonal,
        1.0F);
    CONTRACT_EXPECT(!missing.has_value());
    CONTRACT_EXPECT_EQ(
        missing.error().code,
        runtime::PlayerControllerErrorCode::player_not_found);

    auto collision_world =
        collision::StaticCollisionWorld::create(
            grounding_scene());
    CONTRACT_EXPECT(collision_world.has_value());
    const runtime::PlayerController grounded_controller(
        scene::EntityId("player.synthetic"),
        collision_world.value(),
        {
            20.0F,
            180.0F,
            25.0F,
            100.0F
        },
        10.0F,
        2.0F);
    const auto grounded = grounded_controller.update(
        observation_with_player(),
        idle,
        1.0F);
    CONTRACT_EXPECT(grounded.has_value());
    CONTRACT_EXPECT(grounded.value().has_value());
    if (grounded.has_value() &&
        grounded.value().has_value()) {
        const auto& command =
            std::get<runtime::SetEntityTransformCommand>(
                grounded.value().value());
        CONTRACT_EXPECT_EQ(
            command.transform.position[1],
            5.0F);
    }

    auto session = runtime::RuntimeSession::create(
        exploration_world(),
        1ms,
        std::size_t{4},
        std::size_t{8});
    CONTRACT_EXPECT(session.has_value());
    const runtime::ExplorationObjectiveSystem exploration(
        scene::EntityId("player.synthetic"),
        mission::ObjectiveId("objective.explore"),
        {0.0F, 0.0F, 0.0F},
        5.0F);
    const runtime::RuntimeSystem* systems[]{&exploration};
    auto waiting = session.value().advance(1ms, systems);
    CONTRACT_EXPECT(waiting.has_value());
    CONTRACT_EXPECT_EQ(
        session.value().observe().objectives[0].progress,
        runtime::ObjectiveProgress::pending);

    scene::Transform beyond_threshold;
    beyond_threshold.position = {6.0F, 0.0F, 0.0F};
    const auto enqueued = session.value().enqueue(
        runtime::SetEntityTransformCommand{
            scene::EntityId("player.synthetic"),
            beyond_threshold});
    CONTRACT_EXPECT(enqueued.has_value());
    auto completed = session.value().advance(1ms, systems);
    CONTRACT_EXPECT(completed.has_value());
    CONTRACT_EXPECT_EQ(
        session.value().observe().objectives[0].progress,
        runtime::ObjectiveProgress::completed);
    auto remains_completed = session.value().advance(1ms, systems);
    CONTRACT_EXPECT(remains_completed.has_value());
    CONTRACT_EXPECT_EQ(
        remains_completed.value().events.size(),
        std::size_t{0});

    return test::finish();
}

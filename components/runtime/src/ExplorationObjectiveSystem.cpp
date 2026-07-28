#include <contract/runtime/ExplorationObjectiveSystem.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace contract::runtime {

ExplorationObjectiveSystem::ExplorationObjectiveSystem(
    scene::EntityId player,
    mission::ObjectiveId objective,
    std::array<float, 3> origin,
    float completion_distance)
    : player_(std::move(player)),
      objective_(std::move(objective)),
      origin_(origin),
      completion_distance_(completion_distance) {}

core::Result<void, RuntimeSystemFailure>
ExplorationObjectiveSystem::evaluate(
    const RuntimeWorld& world,
    const RuntimeSystemContext& context,
    RuntimeCommandEmitter& emitter) const {
    static_cast<void>(context);
    if (!std::isfinite(completion_distance_) ||
        completion_distance_ <= 0.0F ||
        !std::all_of(
            origin_.begin(),
            origin_.end(),
            [](float value) {
                return std::isfinite(value);
            })) {
        return core::Result<void, RuntimeSystemFailure>::failure(
            {
                RuntimeSystemFailureCode::evaluation_failed,
                "Exploration objective configuration is invalid"
            });
    }
    const auto player = std::find_if(
        world.entities().begin(),
        world.entities().end(),
        [this](const RuntimeEntityState& entity) {
            return entity.definition.id == player_ && entity.enabled;
        });
    if (player == world.entities().end()) {
        return core::Result<void, RuntimeSystemFailure>::failure(
            {
                RuntimeSystemFailureCode::evaluation_failed,
                "Exploration player entity is unavailable"
            });
    }
    const auto objective = std::find_if(
        world.objectives().begin(),
        world.objectives().end(),
        [this](const RuntimeObjectiveState& candidate) {
            return candidate.definition.id == objective_;
        });
    if (objective == world.objectives().end()) {
        return core::Result<void, RuntimeSystemFailure>::failure(
            {
                RuntimeSystemFailureCode::evaluation_failed,
                "Exploration objective is unavailable"
            });
    }
    if (objective->progress != ObjectiveProgress::pending) {
        return core::Result<void, RuntimeSystemFailure>::success();
    }

    const auto delta_x =
        player->definition.transform.position[0] - origin_[0];
    const auto delta_z =
        player->definition.transform.position[2] - origin_[2];
    const auto distance_squared =
        delta_x * delta_x + delta_z * delta_z;
    if (distance_squared <
        completion_distance_ * completion_distance_) {
        return core::Result<void, RuntimeSystemFailure>::success();
    }
    return emitter.emit(CompleteObjectiveCommand{objective_});
}

}

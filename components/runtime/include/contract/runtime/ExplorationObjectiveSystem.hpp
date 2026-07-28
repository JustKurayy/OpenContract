#pragma once

#include <contract/runtime/RuntimeSystem.hpp>

#include <array>

namespace contract::runtime {

class ExplorationObjectiveSystem final : public RuntimeSystem {
public:
    ExplorationObjectiveSystem(
        scene::EntityId player,
        mission::ObjectiveId objective,
        std::array<float, 3> origin,
        float completion_distance);

    [[nodiscard]] core::Result<void, RuntimeSystemFailure> evaluate(
        const RuntimeWorld& world,
        const RuntimeSystemContext& context,
        RuntimeCommandEmitter& emitter) const override;

private:
    scene::EntityId player_;
    mission::ObjectiveId objective_;
    std::array<float, 3> origin_;
    float completion_distance_{0.0F};
};

}

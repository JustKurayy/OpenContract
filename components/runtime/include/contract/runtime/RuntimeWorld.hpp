#pragma once

#include <contract/core/Result.hpp>
#include <contract/mission/Mission.hpp>
#include <contract/modding/PackageSet.hpp>
#include <contract/navigation/Navigation.hpp>
#include <contract/scene/Scene.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace contract::runtime {

enum class ObjectiveProgress {
    pending,
    completed,
    failed
};

struct RuntimeEntityState {
    scene::EntityDefinition definition;
    bool enabled{true};
};

struct RuntimeObjectiveState {
    mission::MissionObjective definition;
    ObjectiveProgress progress{ObjectiveProgress::pending};
};

enum class RuntimeWorldErrorCode {
    mission_not_found,
    mission_ambiguous,
    map_not_found,
    navigation_not_found,
    entity_not_found,
    invalid_transform,
    objective_not_found,
    invalid_objective_transition
};

struct RuntimeWorldError {
    RuntimeWorldErrorCode code{RuntimeWorldErrorCode::mission_not_found};
    std::string message;
};

class RuntimeWorld {
public:
    RuntimeWorld(
        mission::MissionId mission_id,
        scene::MapId map_id,
        std::optional<navigation::NavigationGraph> navigation_graph,
        std::vector<RuntimeEntityState> entities,
        std::vector<RuntimeObjectiveState> objectives);

    [[nodiscard]] const mission::MissionId& mission_id() const noexcept;
    [[nodiscard]] const scene::MapId& map_id() const noexcept;
    [[nodiscard]] const std::optional<navigation::NavigationGraph>&
    navigation_graph() const noexcept;
    [[nodiscard]] std::span<const RuntimeEntityState> entities() const noexcept;
    [[nodiscard]] std::span<const RuntimeObjectiveState> objectives() const noexcept;
    [[nodiscard]] bool all_objectives_complete() const noexcept;

    [[nodiscard]] core::Result<void, RuntimeWorldError> set_entity_transform(
        const scene::EntityId& entity,
        const scene::Transform& transform);
    [[nodiscard]] core::Result<void, RuntimeWorldError> set_entity_enabled(
        const scene::EntityId& entity,
        bool enabled);
    [[nodiscard]] core::Result<void, RuntimeWorldError> complete_objective(
        const mission::ObjectiveId& objective);
    [[nodiscard]] core::Result<void, RuntimeWorldError> fail_objective(
        const mission::ObjectiveId& objective);

private:
    [[nodiscard]] core::Result<void, RuntimeWorldError> transition_objective(
        const mission::ObjectiveId& objective,
        ObjectiveProgress progress);

    mission::MissionId mission_id_;
    scene::MapId map_id_;
    std::optional<navigation::NavigationGraph> navigation_graph_;
    std::vector<RuntimeEntityState> entities_;
    std::vector<RuntimeObjectiveState> objectives_;
};

class RuntimeWorldBuilder {
public:
    [[nodiscard]] core::Result<RuntimeWorld, RuntimeWorldError> build(
        const modding::LoadedPackageSet& package_set,
        std::string_view mission_id) const;
};

}

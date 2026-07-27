#include <contract/runtime/RuntimeWorld.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace contract::runtime {

RuntimeWorld::RuntimeWorld(
    mission::MissionId mission_id,
    scene::MapId map_id,
    std::optional<navigation::NavigationGraph> navigation_graph,
    std::vector<RuntimeEntityState> entities,
    std::vector<RuntimeObjectiveState> objectives)
    : mission_id_(std::move(mission_id)),
      map_id_(std::move(map_id)),
      navigation_graph_(std::move(navigation_graph)),
      entities_(std::move(entities)),
      objectives_(std::move(objectives)) {}

const mission::MissionId& RuntimeWorld::mission_id() const noexcept {
    return mission_id_;
}

const scene::MapId& RuntimeWorld::map_id() const noexcept {
    return map_id_;
}

const std::optional<navigation::NavigationGraph>&
RuntimeWorld::navigation_graph() const noexcept {
    return navigation_graph_;
}

std::span<const RuntimeEntityState> RuntimeWorld::entities() const noexcept {
    return entities_;
}

std::span<const RuntimeObjectiveState> RuntimeWorld::objectives() const noexcept {
    return objectives_;
}

bool RuntimeWorld::all_objectives_complete() const noexcept {
    return std::all_of(
        objectives_.begin(),
        objectives_.end(),
        [](const RuntimeObjectiveState& objective) {
            return objective.progress == ObjectiveProgress::completed;
        });
}

core::Result<void, RuntimeWorldError> RuntimeWorld::set_entity_transform(
    const scene::EntityId& entity,
    const scene::Transform& transform) {
    const auto match = std::find_if(
        entities_.begin(),
        entities_.end(),
        [&entity](const RuntimeEntityState& candidate) {
            return candidate.definition.id == entity;
        });
    if (match == entities_.end()) {
        return core::Result<void, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::entity_not_found,
                "Entity was not found: " + entity.value()
            });
    }
    if (!scene::is_valid_transform(transform)) {
        return core::Result<void, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::invalid_transform,
                "Entity transform contains a non-finite value: " +
                    entity.value()
            });
    }
    match->definition.transform = transform;
    return core::Result<void, RuntimeWorldError>::success();
}

core::Result<void, RuntimeWorldError> RuntimeWorld::set_entity_enabled(
    const scene::EntityId& entity,
    bool enabled) {
    const auto match = std::find_if(
        entities_.begin(),
        entities_.end(),
        [&entity](const RuntimeEntityState& candidate) {
            return candidate.definition.id == entity;
        });
    if (match == entities_.end()) {
        return core::Result<void, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::entity_not_found,
                "Entity was not found: " + entity.value()
            });
    }
    match->enabled = enabled;
    return core::Result<void, RuntimeWorldError>::success();
}

core::Result<void, RuntimeWorldError> RuntimeWorld::transition_objective(
    const mission::ObjectiveId& objective,
    ObjectiveProgress progress) {
    const auto match = std::find_if(
        objectives_.begin(),
        objectives_.end(),
        [&objective](const RuntimeObjectiveState& candidate) {
            return candidate.definition.id == objective;
        });
    if (match == objectives_.end()) {
        return core::Result<void, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::objective_not_found,
                "Objective was not found: " + objective.value()
            });
    }
    if (match->progress != ObjectiveProgress::pending) {
        return core::Result<void, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::invalid_objective_transition,
                "Only a pending objective can transition: " +
                    objective.value()
            });
    }
    match->progress = progress;
    return core::Result<void, RuntimeWorldError>::success();
}

core::Result<void, RuntimeWorldError> RuntimeWorld::complete_objective(
    const mission::ObjectiveId& objective) {
    return transition_objective(objective, ObjectiveProgress::completed);
}

core::Result<void, RuntimeWorldError> RuntimeWorld::fail_objective(
    const mission::ObjectiveId& objective) {
    return transition_objective(objective, ObjectiveProgress::failed);
}

core::Result<RuntimeWorld, RuntimeWorldError> RuntimeWorldBuilder::build(
    const modding::LoadedPackageSet& package_set,
    std::string_view mission_id) const {
    const modding::ModPackage* selected_package = nullptr;
    const mission::MissionDefinition* selected_mission = nullptr;
    std::size_t matches = 0;

    for (const auto& package : package_set.packages) {
        for (const auto& candidate : package.missions) {
            if (candidate.id.value() == mission_id) {
                ++matches;
                selected_package = &package;
                selected_mission = &candidate;
            }
        }
    }

    if (matches == 0) {
        return core::Result<RuntimeWorld, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::mission_not_found,
                "Mission was not found: " + std::string(mission_id)
            });
    }
    if (matches != 1) {
        return core::Result<RuntimeWorld, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::mission_ambiguous,
                "Mission is declared by more than one package: " +
                    std::string(mission_id)
            });
    }

    const auto map = std::find_if(
        selected_package->maps.begin(),
        selected_package->maps.end(),
        [selected_mission](const scene::MapDefinition& candidate) {
            return candidate.id == selected_mission->map;
        });
    if (map == selected_package->maps.end()) {
        return core::Result<RuntimeWorld, RuntimeWorldError>::failure(
            {
                RuntimeWorldErrorCode::map_not_found,
                "Mission map was not found: " + selected_mission->map.value()
            });
    }

    std::optional<navigation::NavigationGraph> navigation_graph;
    if (map->navigation.has_value()) {
        const auto graph = std::find_if(
            selected_package->navigation_graphs.begin(),
            selected_package->navigation_graphs.end(),
            [map](const navigation::NavigationGraph& candidate) {
                return candidate.id == *map->navigation;
            });
        if (graph == selected_package->navigation_graphs.end()) {
            return core::Result<RuntimeWorld, RuntimeWorldError>::failure(
                {
                    RuntimeWorldErrorCode::navigation_not_found,
                    "Map navigation graph was not found: " +
                        map->navigation->value()
                });
        }
        navigation_graph = *graph;
    }

    std::vector<RuntimeEntityState> entities;
    entities.reserve(map->entities.size());
    for (const auto& entity : map->entities) {
        entities.push_back({entity, true});
    }

    std::vector<RuntimeObjectiveState> objectives;
    objectives.reserve(selected_mission->objectives.size());
    for (const auto& objective : selected_mission->objectives) {
        objectives.push_back({objective, ObjectiveProgress::pending});
    }

    return core::Result<RuntimeWorld, RuntimeWorldError>::success(
        RuntimeWorld(
            selected_mission->id,
            map->id,
            std::move(navigation_graph),
            std::move(entities),
            std::move(objectives)));
}

}

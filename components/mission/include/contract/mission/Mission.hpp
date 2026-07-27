#pragma once

#include <contract/core/Identifier.hpp>
#include <contract/scene/Scene.hpp>

#include <string>
#include <vector>

namespace contract::mission {

using MissionId = core::Identifier<struct MissionIdTag>;
using ObjectiveId = core::Identifier<struct ObjectiveIdTag>;

struct MissionObjective {
    ObjectiveId id;
    std::vector<scene::EntityId> target_references;
    std::vector<scene::EntityId> spawn_references;
};

struct MissionDefinition {
    MissionId id;
    scene::MapId map;
    std::vector<MissionObjective> objectives;
};

enum class MissionIssueCode {
    invalid_mission_identifier,
    invalid_objective_identifier,
    duplicate_objective_identifier,
    map_reference_mismatch,
    missing_target_reference,
    missing_spawn_reference
};

struct MissionIssue {
    MissionIssueCode code{MissionIssueCode::invalid_mission_identifier};
    std::string message;
};

class MissionValidator {
public:
    [[nodiscard]] std::vector<MissionIssue> validate(
        const MissionDefinition& mission,
        const scene::MapDefinition& map) const;
};

}

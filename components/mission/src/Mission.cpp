#include <contract/mission/Mission.hpp>

#include <unordered_set>

namespace contract::mission {

std::vector<MissionIssue> MissionValidator::validate(
    const MissionDefinition& mission,
    const scene::MapDefinition& map) const {
    std::vector<MissionIssue> issues;
    if (!mission.id.valid()) {
        issues.push_back(
            {MissionIssueCode::invalid_mission_identifier,
             "Mission identifier is empty"});
    }
    if (!mission.map.valid() || mission.map != map.id) {
        issues.push_back(
            {MissionIssueCode::map_reference_mismatch,
             "Mission does not reference the supplied map"});
    }

    std::unordered_set<std::string> entities;
    for (const auto& entity : map.entities) {
        if (entity.id.valid()) {
            entities.insert(entity.id.value());
        }
    }

    std::unordered_set<std::string> objective_identifiers;
    for (const auto& objective : mission.objectives) {
        if (!objective.id.valid()) {
            issues.push_back(
                {MissionIssueCode::invalid_objective_identifier,
                 "Mission objective identifier is empty"});
        } else if (!objective_identifiers.insert(objective.id.value()).second) {
            issues.push_back(
                {MissionIssueCode::duplicate_objective_identifier,
                 "Duplicate mission objective identifier: " + objective.id.value()});
        }

        for (const auto& target : objective.target_references) {
            if (!target.valid() || !entities.contains(target.value())) {
                issues.push_back(
                    {MissionIssueCode::missing_target_reference,
                     "Mission objective references an undeclared target entity: " +
                         target.value()});
            }
        }
        for (const auto& spawn : objective.spawn_references) {
            if (!spawn.valid() || !entities.contains(spawn.value())) {
                issues.push_back(
                    {MissionIssueCode::missing_spawn_reference,
                     "Mission objective references an undeclared spawn entity: " +
                         spawn.value()});
            }
        }
    }

    return issues;
}

}

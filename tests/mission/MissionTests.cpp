#include "TestSupport.hpp"

#include <contract/mission/Mission.hpp>
#include <contract/mission/SourceMissionLoader.hpp>

#include <vector>

namespace {

bool contains_issue(
    const std::vector<contract::mission::MissionIssue>& issues,
    contract::mission::MissionIssueCode code) {
    for (const auto& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

contract::scene::MapDefinition valid_map() {
    using namespace contract::scene;

    return {
        MapId("map.synthetic"),
        {
            EntityDefinition{EntityId("entity.spawn"), Transform{}, {}},
            EntityDefinition{EntityId("entity.target"), Transform{}, {}}
        }};
}

contract::mission::MissionDefinition valid_mission() {
    using namespace contract;

    return {
        mission::MissionId("mission.synthetic"),
        scene::MapId("map.synthetic"),
        {
            mission::MissionObjective{
                mission::ObjectiveId("objective.primary"),
                {scene::EntityId("entity.target")},
                {scene::EntityId("entity.spawn")}}
        }};
}

}

int main() {
    CONTRACT_EXPECT(
        contract::mission::is_valid_source_mission_id("M00"));
    CONTRACT_EXPECT(
        contract::mission::is_valid_source_mission_id("custom_scene-1"));
    CONTRACT_EXPECT(
        !contract::mission::is_valid_source_mission_id("../M00"));
    CONTRACT_EXPECT(
        !contract::mission::is_valid_source_mission_id(""));

    using namespace contract::mission;

    MissionValidator validator;
    CONTRACT_EXPECT(validator.validate(valid_mission(), valid_map()).empty());

    auto duplicate_objective = valid_mission();
    duplicate_objective.objectives.push_back(duplicate_objective.objectives.front());
    CONTRACT_EXPECT(contains_issue(
        validator.validate(duplicate_objective, valid_map()),
        MissionIssueCode::duplicate_objective_identifier));

    auto missing_target = valid_mission();
    missing_target.objectives.front().target_references = {
        contract::scene::EntityId("entity.missing")};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(missing_target, valid_map()),
        MissionIssueCode::missing_target_reference));

    auto missing_spawn = valid_mission();
    missing_spawn.objectives.front().spawn_references = {
        contract::scene::EntityId("entity.missing")};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(missing_spawn, valid_map()),
        MissionIssueCode::missing_spawn_reference));

    auto wrong_map = valid_mission();
    wrong_map.map = contract::scene::MapId("map.other");
    CONTRACT_EXPECT(contains_issue(
        validator.validate(wrong_map, valid_map()),
        MissionIssueCode::map_reference_mismatch));

    return contract::test::finish();
}

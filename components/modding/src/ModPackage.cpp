#include <contract/modding/ModPackage.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace contract::modding {
namespace {

template <typename Identifier>
void check_identifier(
    const Identifier& identifier,
    std::string_view kind,
    std::unordered_set<std::string>& seen,
    std::vector<ModelIssue>& issues) {
    if (!identifier.valid()) {
        issues.push_back(
            {ModelIssueCode::invalid_identifier, std::string(kind) + " identifier is empty"});
        return;
    }
    if (!seen.insert(identifier.value()).second) {
        issues.push_back(
            {ModelIssueCode::duplicate_identifier,
             "Duplicate " + std::string(kind) + " identifier: " + identifier.value()});
    }
}

void append_asset_issues(
    const std::vector<assets::AssetIssue>& source,
    std::vector<ModelIssue>& destination) {
    for (const auto& issue : source) {
        switch (issue.code) {
        case assets::AssetIssueCode::invalid_identifier:
            destination.push_back({ModelIssueCode::invalid_identifier, issue.message});
            break;
        case assets::AssetIssueCode::duplicate_identifier:
            destination.push_back({ModelIssueCode::duplicate_identifier, issue.message});
            break;
        case assets::AssetIssueCode::invalid_source_path:
            destination.push_back({ModelIssueCode::invalid_asset_source, issue.message});
            break;
        }
    }
}

void append_scene_issues(
    const std::vector<scene::SceneIssue>& source,
    std::vector<ModelIssue>& destination) {
    for (const auto& issue : source) {
        switch (issue.code) {
        case scene::SceneIssueCode::invalid_map_identifier:
        case scene::SceneIssueCode::invalid_entity_identifier:
            destination.push_back({ModelIssueCode::invalid_identifier, issue.message});
            break;
        case scene::SceneIssueCode::duplicate_entity_identifier:
            destination.push_back({ModelIssueCode::duplicate_identifier, issue.message});
            break;
        case scene::SceneIssueCode::invalid_transform:
        case scene::SceneIssueCode::invalid_component_reference:
            destination.push_back({ModelIssueCode::invalid_scene, issue.message});
            break;
        }
    }
}

void append_mission_issues(
    const std::vector<mission::MissionIssue>& source,
    std::vector<ModelIssue>& destination) {
    for (const auto& issue : source) {
        switch (issue.code) {
        case mission::MissionIssueCode::invalid_mission_identifier:
        case mission::MissionIssueCode::invalid_objective_identifier:
            destination.push_back({ModelIssueCode::invalid_identifier, issue.message});
            break;
        case mission::MissionIssueCode::duplicate_objective_identifier:
            destination.push_back({ModelIssueCode::duplicate_identifier, issue.message});
            break;
        case mission::MissionIssueCode::map_reference_mismatch:
            destination.push_back({ModelIssueCode::missing_map_reference, issue.message});
            break;
        case mission::MissionIssueCode::missing_target_reference:
        case mission::MissionIssueCode::missing_spawn_reference:
            destination.push_back({ModelIssueCode::missing_entity_reference, issue.message});
            break;
        }
    }
}

}

std::vector<ModelIssue> ModPackageValidator::validate(const ModPackage& package) const {
    std::vector<ModelIssue> issues;

    std::unordered_set<std::string> package_identifiers;
    check_identifier(package.id, "package", package_identifiers, issues);

    std::unordered_set<std::string> dependency_identifiers;
    for (const auto& dependency : package.dependencies) {
        check_identifier(dependency.package, "dependency", dependency_identifiers, issues);
    }

    const assets::AssetCatalogValidator asset_validator;
    append_asset_issues(asset_validator.validate(package.assets), issues);

    std::unordered_set<std::string> asset_identifiers;
    for (const auto& asset : package.assets) {
        if (asset.id.valid()) {
            asset_identifiers.insert(asset.id.value());
        }
    }

    const navigation::NavigationValidator navigation_validator;
    std::unordered_set<std::string> navigation_identifiers;
    for (const auto& graph : package.navigation_graphs) {
        check_identifier(graph.id, "navigation graph", navigation_identifiers, issues);
        const auto graph_issues = navigation_validator.validate(graph);
        for (const auto& issue : graph_issues) {
            issues.push_back({ModelIssueCode::invalid_navigation, issue.message});
        }
    }

    const scene::SceneValidator scene_validator;
    std::unordered_set<std::string> map_identifiers;
    std::unordered_map<std::string, const scene::MapDefinition*> maps;
    for (const auto& map : package.maps) {
        check_identifier(map.id, "map", map_identifiers, issues);
        if (map.id.valid()) {
            maps.try_emplace(map.id.value(), &map);
        }
        append_scene_issues(scene_validator.validate(map), issues);

        if (map.navigation.has_value() &&
            (!map.navigation->valid() ||
             !navigation_identifiers.contains(map.navigation->value()))) {
            issues.push_back(
                {ModelIssueCode::missing_navigation_reference,
                 "Map references an undeclared navigation graph: " +
                     map.navigation->value()});
        }

        for (const auto& entity : map.entities) {
            for (const auto& component : entity.components) {
                for (const auto& asset : component.assets) {
                    if (!asset.valid() || !asset_identifiers.contains(asset.value())) {
                        issues.push_back(
                            {ModelIssueCode::missing_asset_reference,
                             "Component references an undeclared asset: " + asset.value()});
                    }
                }
            }
        }
    }

    const mission::MissionValidator mission_validator;
    std::unordered_set<std::string> mission_identifiers;
    for (const auto& mission : package.missions) {
        check_identifier(mission.id, "mission", mission_identifiers, issues);
        const auto map = maps.find(mission.map.value());
        if (!mission.map.valid() || map == maps.end()) {
            issues.push_back(
                {ModelIssueCode::missing_map_reference,
                 "Mission references an undeclared map: " + mission.map.value()});
            continue;
        }
        append_mission_issues(mission_validator.validate(mission, *map->second), issues);
    }

    return issues;
}

}

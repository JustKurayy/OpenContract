#include "TestSupport.hpp"

#include <contract/modding/ModPackage.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

bool contains_issue(
    const std::vector<contract::modding::ModelIssue>& issues,
    contract::modding::ModelIssueCode code) {
    return std::ranges::any_of(issues, [code](const auto& issue) {
        return issue.code == code;
    });
}

contract::modding::ModPackage valid_package() {
    using namespace contract;

    assets::AssetDefinition mesh{
        assets::AssetId("mesh.main"),
        std::filesystem::path("meshes/main.mesh")};

    navigation::NavigationGraph navigation_graph{
        navigation::NavigationGraphId("navigation.main"),
        {
            navigation::NavigationNode{
                navigation::NavigationNodeId("node.spawn"),
                {0.0F, 0.0F, 0.0F},
                {}}
        }};

    scene::EntityDefinition spawn{
        scene::EntityId("entity.spawn"),
        scene::Transform{},
        {
            scene::ComponentReference{
                "visual",
                {assets::AssetId("mesh.main")}}
        }};
    scene::EntityDefinition target{
        scene::EntityId("entity.target"),
        scene::Transform{},
        {}};
    scene::MapDefinition map{
        scene::MapId("map.main"),
        {spawn, target},
        navigation::NavigationGraphId("navigation.main")};

    mission::MissionObjective objective{
        mission::ObjectiveId("objective.primary"),
        {scene::EntityId("entity.target")},
        {scene::EntityId("entity.spawn")}};
    mission::MissionDefinition mission{
        mission::MissionId("mission.main"),
        scene::MapId("map.main"),
        {objective}};

    return {
        modding::ModPackageId("example.package"),
        modding::SemanticVersion{1, 0, 0},
        modding::PackageMetadata{
            "Synthetic Package",
            "Test Author",
            "Synthetic data only"},
        {
            modding::PackageDependency{
                modding::ModPackageId("example.base"),
                modding::SemanticVersion{1, 2, 0}}
        },
        {mesh},
        {navigation_graph},
        {map},
        {mission}};
}

}

int main() {
    using namespace contract;

    modding::ModPackageValidator validator;
    CONTRACT_EXPECT(validator.validate(valid_package()).empty());

    auto duplicate_entity = valid_package();
    duplicate_entity.maps[0].entities.push_back(
        duplicate_entity.maps[0].entities[0]);
    CONTRACT_EXPECT(contains_issue(
        validator.validate(duplicate_entity),
        modding::ModelIssueCode::duplicate_identifier));

    auto missing_target = valid_package();
    missing_target.missions[0].objectives[0].target_references = {
        scene::EntityId("entity.missing")};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(missing_target),
        modding::ModelIssueCode::missing_entity_reference));

    auto missing_map = valid_package();
    missing_map.missions[0].map = scene::MapId("map.missing");
    CONTRACT_EXPECT(contains_issue(
        validator.validate(missing_map),
        modding::ModelIssueCode::missing_map_reference));

    auto missing_asset = valid_package();
    missing_asset.maps[0].entities[0].components[0].assets = {
        assets::AssetId("mesh.missing")};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(missing_asset),
        modding::ModelIssueCode::missing_asset_reference));

    auto missing_navigation = valid_package();
    missing_navigation.maps[0].navigation =
        navigation::NavigationGraphId("navigation.missing");
    CONTRACT_EXPECT(contains_issue(
        validator.validate(missing_navigation),
        modding::ModelIssueCode::missing_navigation_reference));

    auto invalid_navigation = valid_package();
    invalid_navigation.navigation_graphs[0].nodes[0].neighbors = {
        navigation::NavigationNodeId("node.missing")};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(invalid_navigation),
        modding::ModelIssueCode::invalid_navigation));

    auto invalid_package = valid_package();
    invalid_package.id = modding::ModPackageId("");
    CONTRACT_EXPECT(contains_issue(
        validator.validate(invalid_package),
        modding::ModelIssueCode::invalid_identifier));

    auto unsafe_package = valid_package();
    unsafe_package.id = modding::ModPackageId("package/unsafe");
    CONTRACT_EXPECT(contains_issue(
        validator.validate(unsafe_package),
        modding::ModelIssueCode::invalid_identifier));

    return contract::test::finish();
}

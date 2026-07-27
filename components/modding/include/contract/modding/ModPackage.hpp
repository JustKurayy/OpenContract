#pragma once

#include <contract/assets/Asset.hpp>
#include <contract/core/Identifier.hpp>
#include <contract/mission/Mission.hpp>
#include <contract/navigation/Navigation.hpp>
#include <contract/scene/Scene.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace contract::modding {

using ModPackageId = core::Identifier<struct ModPackageIdTag>;

struct SemanticVersion {
    std::uint32_t major{0};
    std::uint32_t minor{0};
    std::uint32_t patch{0};

    auto operator<=>(const SemanticVersion&) const = default;
};

struct PackageMetadata {
    std::string name;
    std::string author;
    std::string description;
};

struct PackageDependency {
    ModPackageId package;
    SemanticVersion minimum_version;
};

struct ModPackage {
    ModPackageId id;
    SemanticVersion version;
    PackageMetadata metadata;
    std::vector<PackageDependency> dependencies;
    std::vector<assets::AssetDefinition> assets;
    std::vector<navigation::NavigationGraph> navigation_graphs;
    std::vector<scene::MapDefinition> maps;
    std::vector<mission::MissionDefinition> missions;
};

enum class ModelIssueCode {
    invalid_identifier,
    duplicate_identifier,
    invalid_asset_source,
    invalid_scene,
    invalid_navigation,
    missing_asset_reference,
    missing_navigation_reference,
    missing_map_reference,
    missing_entity_reference
};

struct ModelIssue {
    ModelIssueCode code{ModelIssueCode::invalid_identifier};
    std::string message;
};

class ModPackageValidator {
public:
    [[nodiscard]] std::vector<ModelIssue> validate(const ModPackage& package) const;
};

}

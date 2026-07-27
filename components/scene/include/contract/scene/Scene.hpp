#pragma once

#include <contract/assets/Asset.hpp>
#include <contract/core/Identifier.hpp>
#include <contract/navigation/Navigation.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace contract::scene {

using MapId = core::Identifier<struct MapIdTag>;
using EntityId = core::Identifier<struct EntityIdTag>;

struct Transform {
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
};

struct ComponentReference {
    std::string type;
    std::vector<assets::AssetId> assets;
};

struct EntityDefinition {
    EntityId id;
    Transform transform;
    std::vector<ComponentReference> components;
};

struct MapDefinition {
    MapId id;
    std::vector<EntityDefinition> entities;
    std::optional<navigation::NavigationGraphId> navigation;
};

[[nodiscard]] bool is_valid_transform(const Transform& transform) noexcept;

enum class SceneIssueCode {
    invalid_map_identifier,
    invalid_entity_identifier,
    duplicate_entity_identifier,
    invalid_transform,
    invalid_component_reference
};

struct SceneIssue {
    SceneIssueCode code{SceneIssueCode::invalid_map_identifier};
    std::string message;
};

class SceneValidator {
public:
    [[nodiscard]] std::vector<SceneIssue> validate(const MapDefinition& map) const;
};

}

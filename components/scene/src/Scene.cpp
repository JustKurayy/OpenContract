#include <contract/scene/Scene.hpp>

#include <cmath>
#include <unordered_set>

namespace contract::scene {
namespace {

template <std::size_t Size>
bool all_finite(const std::array<float, Size>& values) noexcept {
    for (const auto value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

}

bool is_valid_transform(const Transform& transform) noexcept {
    return all_finite(transform.position) &&
           all_finite(transform.rotation) &&
           all_finite(transform.scale);
}

std::vector<SceneIssue> SceneValidator::validate(const MapDefinition& map) const {
    std::vector<SceneIssue> issues;
    if (!map.id.valid()) {
        issues.push_back(
            {SceneIssueCode::invalid_map_identifier, "Map identifier is empty"});
    }

    std::unordered_set<std::string> entity_identifiers;
    for (const auto& entity : map.entities) {
        if (!entity.id.valid()) {
            issues.push_back(
                {SceneIssueCode::invalid_entity_identifier, "Entity identifier is empty"});
        } else if (!entity_identifiers.insert(entity.id.value()).second) {
            issues.push_back(
                {SceneIssueCode::duplicate_entity_identifier,
                 "Duplicate entity identifier: " + entity.id.value()});
        }

        if (!is_valid_transform(entity.transform)) {
            issues.push_back(
                {SceneIssueCode::invalid_transform,
                 "Entity transform contains a non-finite value"});
        }

        for (const auto& component : entity.components) {
            bool valid = !component.type.empty();
            for (const auto& asset : component.assets) {
                valid = valid && asset.valid();
            }
            if (!valid) {
                issues.push_back(
                    {SceneIssueCode::invalid_component_reference,
                     "Component type and asset identifiers must be non-empty"});
            }
        }
    }

    return issues;
}

}

#include "TestSupport.hpp"

#include <contract/scene/Scene.hpp>

#include <limits>
#include <vector>

namespace {

bool contains_issue(
    const std::vector<contract::scene::SceneIssue>& issues,
    contract::scene::SceneIssueCode code) {
    for (const auto& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

contract::scene::MapDefinition valid_map() {
    using namespace contract;

    return {
        scene::MapId("map.synthetic"),
        {
            scene::EntityDefinition{
                scene::EntityId("entity.spawn"),
                scene::Transform{},
                {
                    scene::ComponentReference{
                        "visual",
                        {assets::AssetId("mesh.main")}}
                }}
        }};
}

}

int main() {
    using namespace contract::scene;

    SceneValidator validator;
    CONTRACT_EXPECT(validator.validate(valid_map()).empty());

    auto duplicates = valid_map();
    duplicates.entities.push_back(duplicates.entities.front());
    CONTRACT_EXPECT(contains_issue(
        validator.validate(duplicates),
        SceneIssueCode::duplicate_entity_identifier));

    auto invalid_transform = valid_map();
    invalid_transform.entities.front().transform.position[0] =
        std::numeric_limits<float>::infinity();
    CONTRACT_EXPECT(contains_issue(
        validator.validate(invalid_transform),
        SceneIssueCode::invalid_transform));

    auto empty_component = valid_map();
    empty_component.entities.front().components.front().type.clear();
    CONTRACT_EXPECT(contains_issue(
        validator.validate(empty_component),
        SceneIssueCode::invalid_component_reference));

    return contract::test::finish();
}

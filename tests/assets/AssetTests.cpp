#include "TestSupport.hpp"

#include <contract/assets/Asset.hpp>

#include <filesystem>
#include <vector>

namespace {

bool contains_issue(
    const std::vector<contract::assets::AssetIssue>& issues,
    contract::assets::AssetIssueCode code) {
    for (const auto& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

}

int main() {
    using namespace contract::assets;

    AssetCatalogValidator validator;
    const std::vector valid{
        AssetDefinition{AssetId("mesh.main"), std::filesystem::path("meshes/main.mesh")},
        AssetDefinition{AssetId("sound.ambient"), std::filesystem::path("audio/ambient.sound")}};
    CONTRACT_EXPECT(validator.validate(valid).empty());

    auto duplicates = valid;
    duplicates.push_back(valid.front());
    CONTRACT_EXPECT(contains_issue(
        validator.validate(duplicates),
        AssetIssueCode::duplicate_identifier));

    const std::vector traversal{
        AssetDefinition{AssetId("mesh.escape"), std::filesystem::path("../outside.mesh")}};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(traversal),
        AssetIssueCode::invalid_source_path));

    const std::vector absolute{
        AssetDefinition{AssetId("mesh.absolute"), std::filesystem::path("C:/outside.mesh")}};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(absolute),
        AssetIssueCode::invalid_source_path));

    const std::vector current_directory{
        AssetDefinition{AssetId("mesh.current"), std::filesystem::path("./inside.mesh")}};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(current_directory),
        AssetIssueCode::invalid_source_path));

    return contract::test::finish();
}

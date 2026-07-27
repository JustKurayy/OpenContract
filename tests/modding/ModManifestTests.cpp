#include "TestSupport.hpp"

#include <contract/modding/ModManifest.hpp>

#include <contract/assets/Asset.hpp>
#include <contract/mission/Mission.hpp>
#include <contract/scene/Scene.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                "contract-synthetic-manifest-test") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

contract::modding::ModPackage synthetic_package() {
    using namespace contract;

    const assets::AssetDefinition asset{
        assets::AssetId("mesh.synthetic"),
        std::filesystem::path("meshes/synthetic.mesh")};
    const scene::EntityDefinition target{
        scene::EntityId("entity.target"),
        scene::Transform{},
        {
            scene::ComponentReference{
                "visual",
                {assets::AssetId("mesh.synthetic")}}
        }};
    const scene::MapDefinition map{
        scene::MapId("map.synthetic"),
        {target},
        std::nullopt};
    const mission::MissionDefinition mission{
        mission::MissionId("mission.synthetic"),
        scene::MapId("map.synthetic"),
        {
            mission::MissionObjective{
                mission::ObjectiveId("objective.synthetic"),
                {scene::EntityId("entity.target")},
                {scene::EntityId("entity.target")}}
        }};

    return {
        modding::ModPackageId("package.synthetic"),
        modding::SemanticVersion{1, 2, 3},
        modding::PackageMetadata{
            "Synthetic Package",
            "Synthetic Author",
            "Synthetic content only"},
        {},
        {asset},
        {},
        {map},
        {mission}};
}

std::string replace_once(
    std::string input,
    const std::string& from,
    const std::string& to) {
    const auto position = input.find(from);
    if (position != std::string::npos) {
        input.replace(position, from.size(), to);
    }
    return input;
}

}

int main() {
    using namespace contract::modding;

    ModManifestCodec codec;
    const auto encoded = codec.serialize(synthetic_package());
    CONTRACT_EXPECT(encoded.has_value());
    CONTRACT_EXPECT(encoded.value().find("\"schema_version\":1") != std::string::npos);

    const auto encoded_again = codec.serialize(synthetic_package());
    CONTRACT_EXPECT(encoded_again.has_value());
    CONTRACT_EXPECT_EQ(encoded.value(), encoded_again.value());

    const auto decoded = codec.parse(encoded.value());
    CONTRACT_EXPECT(decoded.has_value());
    CONTRACT_EXPECT_EQ(decoded.value().id.value(), std::string("package.synthetic"));
    CONTRACT_EXPECT_EQ(decoded.value().version.major, std::uint32_t{1});
    CONTRACT_EXPECT_EQ(decoded.value().assets.size(), std::size_t{1});
    CONTRACT_EXPECT_EQ(
        decoded.value().assets[0].source.generic_string(),
        std::string("meshes/synthetic.mesh"));
    CONTRACT_EXPECT_EQ(decoded.value().maps.size(), std::size_t{1});
    CONTRACT_EXPECT_EQ(decoded.value().missions.size(), std::size_t{1});
    CONTRACT_EXPECT_EQ(
        decoded.value().missions[0].objectives[0].target_references[0].value(),
        std::string("entity.target"));

    const auto malformed = codec.parse("{");
    CONTRACT_EXPECT(!malformed.has_value());
    CONTRACT_EXPECT_EQ(
        malformed.error().code,
        ManifestErrorCode::syntax_error);
    CONTRACT_EXPECT(malformed.error().byte_offset.has_value());

    const auto oversized = codec.parse(std::string(9, 'x'), 8);
    CONTRACT_EXPECT(!oversized.has_value());
    CONTRACT_EXPECT_EQ(
        oversized.error().code,
        ManifestErrorCode::size_limit_exceeded);

    const auto unsupported = codec.parse(replace_once(
        encoded.value(),
        "\"schema_version\":1",
        "\"schema_version\":99"));
    CONTRACT_EXPECT(!unsupported.has_value());
    CONTRACT_EXPECT_EQ(
        unsupported.error().code,
        ManifestErrorCode::unsupported_version);

    const auto missing_package = codec.parse("{\"schema_version\":1}");
    CONTRACT_EXPECT(!missing_package.has_value());
    CONTRACT_EXPECT_EQ(
        missing_package.error().code,
        ManifestErrorCode::schema_error);

    const auto invalid_reference = codec.parse(replace_once(
        encoded.value(),
        "\"targets\":[\"entity.target\"]",
        "\"targets\":[\"entity.missing\"]"));
    CONTRACT_EXPECT(!invalid_reference.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_reference.error().code,
        ManifestErrorCode::validation_error);

    auto invalid_source_package = synthetic_package();
    invalid_source_package.assets[0].source = "../outside.mesh";
    const auto invalid_source = codec.serialize(invalid_source_package);
    CONTRACT_EXPECT(!invalid_source.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_source.error().code,
        ManifestErrorCode::validation_error);

    TemporaryDirectory temporary;
    const auto manifest_path = temporary.path() / "synthetic.contract.json";
    {
        std::ofstream manifest(manifest_path, std::ios::binary);
        manifest << encoded.value();
    }
    const auto file_decoded = codec.parse_file(manifest_path);
    CONTRACT_EXPECT(file_decoded.has_value());
    CONTRACT_EXPECT_EQ(
        file_decoded.value().id.value(),
        std::string("package.synthetic"));

    const auto file_oversized = codec.parse_file(manifest_path, 8);
    CONTRACT_EXPECT(!file_oversized.has_value());
    CONTRACT_EXPECT_EQ(
        file_oversized.error().code,
        ManifestErrorCode::size_limit_exceeded);

    const auto file_missing = codec.parse_file(temporary.path() / "missing.json");
    CONTRACT_EXPECT(!file_missing.has_value());
    CONTRACT_EXPECT_EQ(
        file_missing.error().code,
        ManifestErrorCode::source_error);

    return contract::test::finish();
}

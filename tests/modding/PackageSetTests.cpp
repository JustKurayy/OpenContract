#include "TestSupport.hpp"

#include <contract/modding/ModManifest.hpp>
#include <contract/modding/PackageSet.hpp>

#include <contract/filesystem/ReadOnlyFilesystem.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                "contract-synthetic-package-set-test") {
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

contract::modding::ModPackage package(
    std::string id,
    std::vector<contract::modding::PackageDependency> dependencies = {}) {
    return {
        contract::modding::ModPackageId(std::move(id)),
        {1, 0, 0},
        {"Synthetic", "Synthetic Author", "Synthetic only"},
        std::move(dependencies),
        {},
        {},
        {},
        {}};
}

void write_manifest(
    const std::filesystem::path& path,
    const contract::modding::ModPackage& package_value) {
    const contract::modding::ModManifestCodec codec;
    const auto encoded = codec.serialize(package_value);
    if (encoded.has_value()) {
        std::ofstream output(path, std::ios::binary);
        output << encoded.value();
    }
}

}

int main() {
    using namespace contract::modding;

    const auto base = package("package.base");
    const auto addon = package(
        "package.addon",
        {
            PackageDependency{
                ModPackageId("package.base"),
                SemanticVersion{1, 0, 0}}
        });

    TemporaryDirectory temporary;
    const auto base_path = temporary.path() / "base.contract.json";
    const auto addon_path = temporary.path() / "addon.contract.json";
    write_manifest(base_path, base);
    write_manifest(addon_path, addon);

    PackageSetLoader loader;
    const std::vector paths{addon_path, base_path};
    const auto loaded = loader.load(paths);
    CONTRACT_EXPECT(loaded.has_value());
    CONTRACT_EXPECT_EQ(loaded.value().packages.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(loaded.value().load_order.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(loaded.value().manifest_paths.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(loaded.value().manifest_paths[0], addon_path);
    CONTRACT_EXPECT_EQ(loaded.value().manifest_paths[1], base_path);
    CONTRACT_EXPECT_EQ(
        loaded.value().load_order[0].value(),
        std::string("package.base"));
    CONTRACT_EXPECT_EQ(
        loaded.value().load_order[1].value(),
        std::string("package.addon"));

    const std::vector addon_only{addon_path};
    const auto unresolved = loader.load(addon_only);
    CONTRACT_EXPECT(!unresolved.has_value());
    CONTRACT_EXPECT_EQ(
        unresolved.error().code,
        PackageSetErrorCode::resolution_error);

    const std::vector missing_path{temporary.path() / "missing.json"};
    const auto missing = loader.load(missing_path);
    CONTRACT_EXPECT(!missing.has_value());
    CONTRACT_EXPECT_EQ(
        missing.error().code,
        PackageSetErrorCode::manifest_error);

    auto with_asset = package("package.assets");
    with_asset.assets.push_back(
        {
            contract::assets::AssetId("asset.synthetic"),
            std::filesystem::path("assets/item.bin")
        });
    const auto asset_manifest_path =
        temporary.path() / "assets.contract.json";
    write_manifest(asset_manifest_path, with_asset);
    const std::vector asset_paths{asset_manifest_path};
    const auto asset_set = loader.load(asset_paths);
    CONTRACT_EXPECT(asset_set.has_value());

    std::error_code filesystem_error;
    std::filesystem::create_directories(
        temporary.path() / "assets",
        filesystem_error);
    {
        std::ofstream asset_output(
            temporary.path() / "assets" / "item.bin",
            std::ios::binary);
        asset_output << "synthetic";
    }

    const contract::filesystem::NativeReadOnlyFilesystem filesystem;
    const PackageAssetVerifier verifier(filesystem);
    CONTRACT_EXPECT(verifier.verify(asset_set.value()).empty());

    std::filesystem::remove(
        temporary.path() / "assets" / "item.bin",
        filesystem_error);
    const auto missing_asset = verifier.verify(asset_set.value());
    CONTRACT_EXPECT_EQ(missing_asset.size(), std::size_t{1});
    CONTRACT_EXPECT_EQ(
        missing_asset[0].code,
        PackageAssetIssueCode::asset_location_error);
    CONTRACT_EXPECT_EQ(
        missing_asset[0].package.value(),
        std::string("package.assets"));
    CONTRACT_EXPECT_EQ(
        missing_asset[0].asset.value(),
        std::string("asset.synthetic"));

    auto without_origin = asset_set.value();
    without_origin.manifest_paths.clear();
    const auto missing_origin = verifier.verify(without_origin);
    CONTRACT_EXPECT_EQ(missing_origin.size(), std::size_t{1});
    CONTRACT_EXPECT_EQ(
        missing_origin[0].code,
        PackageAssetIssueCode::missing_manifest_origin);

    return contract::test::finish();
}

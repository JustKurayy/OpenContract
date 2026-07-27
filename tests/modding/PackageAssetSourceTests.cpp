#include "TestSupport.hpp"

#include <contract/modding/PackageAssetSource.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/modding/PackageSet.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                "contract-synthetic-package-asset-source-test") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_ / "assets", error);
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

contract::modding::LoadedPackageSet package_set(
    const std::filesystem::path& manifest_path,
    std::filesystem::path asset_path = "assets/item.bin") {
    using namespace contract;

    modding::LoadedPackageSet set;
    set.packages.push_back(
        {
            modding::ModPackageId("package.synthetic"),
            {1, 0, 0},
            {"Synthetic", "Synthetic Author", "Synthetic only"},
            {},
            {
                assets::AssetDefinition{
                    assets::AssetId("asset.synthetic"),
                    std::move(asset_path)}
            },
            {},
            {},
            {}
        });
    set.load_order.push_back(
        modding::ModPackageId("package.synthetic"));
    set.manifest_paths.push_back(manifest_path);
    return set;
}

}

int main() {
    using namespace contract;

    TemporaryDirectory temporary;
    const auto manifest_path = temporary.path() / "package.contract.json";
    {
        std::ofstream manifest(manifest_path, std::ios::binary);
        manifest << "synthetic";
    }
    {
        std::ofstream asset(
            temporary.path() / "assets" / "item.bin",
            std::ios::binary);
        asset.write("\x10\x20\x30\x40", 4);
    }

    const filesystem::NativeReadOnlyFilesystem filesystem;
    const modding::PackageAssetSource assets(filesystem);
    const auto set = package_set(manifest_path);
    auto opened = assets.open(
        set,
        modding::ModPackageId("package.synthetic"),
        contract::assets::AssetId("asset.synthetic"));
    CONTRACT_EXPECT(opened.has_value());
    CONTRACT_EXPECT_EQ(opened.value().size(), std::uint64_t{4});

    datasource::ReadBudget budget(4, 4);
    const auto bytes = opened.value().read(0, 4, budget);
    CONTRACT_EXPECT(bytes.has_value());
    CONTRACT_EXPECT_EQ(bytes.value().size(), std::size_t{4});
    CONTRACT_EXPECT_EQ(bytes.value()[0], std::byte{0x10});
    CONTRACT_EXPECT_EQ(bytes.value()[3], std::byte{0x40});
    CONTRACT_EXPECT_EQ(budget.consumed(), std::uint64_t{4});

    const auto missing_package = assets.open(
        set,
        modding::ModPackageId("package.missing"),
        contract::assets::AssetId("asset.synthetic"));
    CONTRACT_EXPECT(!missing_package.has_value());
    CONTRACT_EXPECT_EQ(
        missing_package.error().code,
        modding::PackageAssetSourceErrorCode::package_not_found);

    const auto missing_asset = assets.open(
        set,
        modding::ModPackageId("package.synthetic"),
        contract::assets::AssetId("asset.missing"));
    CONTRACT_EXPECT(!missing_asset.has_value());
    CONTRACT_EXPECT_EQ(
        missing_asset.error().code,
        modding::PackageAssetSourceErrorCode::asset_not_found);

    auto missing_origin_set = set;
    missing_origin_set.manifest_paths.clear();
    const auto missing_origin = assets.open(
        missing_origin_set,
        modding::ModPackageId("package.synthetic"),
        contract::assets::AssetId("asset.synthetic"));
    CONTRACT_EXPECT(!missing_origin.has_value());
    CONTRACT_EXPECT_EQ(
        missing_origin.error().code,
        modding::PackageAssetSourceErrorCode::manifest_origin_missing);

    const auto unsafe_set = package_set(manifest_path, "../outside.bin");
    const auto unsafe = assets.open(
        unsafe_set,
        modding::ModPackageId("package.synthetic"),
        contract::assets::AssetId("asset.synthetic"));
    CONTRACT_EXPECT(!unsafe.has_value());
    CONTRACT_EXPECT_EQ(
        unsafe.error().code,
        modding::PackageAssetSourceErrorCode::asset_location_error);

    const auto directory_set = package_set(manifest_path, "assets");
    const auto directory = assets.open(
        directory_set,
        modding::ModPackageId("package.synthetic"),
        contract::assets::AssetId("asset.synthetic"));
    CONTRACT_EXPECT(!directory.has_value());
    CONTRACT_EXPECT_EQ(
        directory.error().code,
        modding::PackageAssetSourceErrorCode::source_open_error);

    return test::finish();
}

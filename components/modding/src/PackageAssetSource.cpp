#include <contract/modding/PackageAssetSource.hpp>

#include <contract/assets/AssetLocator.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace contract::modding {

PackageAssetSource::PackageAssetSource(
    const filesystem::IReadOnlyFilesystem& filesystem)
    : filesystem_(filesystem) {}

core::Result<datasource::FileDataSource, PackageAssetSourceError>
PackageAssetSource::open(
    const LoadedPackageSet& package_set,
    const ModPackageId& package,
    const assets::AssetId& asset) const {
    const auto package_match = std::find_if(
        package_set.packages.begin(),
        package_set.packages.end(),
        [&package](const ModPackage& candidate) {
            return candidate.id == package;
        });
    if (package_match == package_set.packages.end()) {
        return core::Result<
            datasource::FileDataSource,
            PackageAssetSourceError>::failure(
            {
                PackageAssetSourceErrorCode::package_not_found,
                package,
                asset,
                "Package was not found: " + package.value()
            });
    }

    const auto package_index = static_cast<std::size_t>(
        std::distance(package_set.packages.begin(), package_match));
    if (package_index >= package_set.manifest_paths.size()) {
        return core::Result<
            datasource::FileDataSource,
            PackageAssetSourceError>::failure(
            {
                PackageAssetSourceErrorCode::manifest_origin_missing,
                package,
                asset,
                "Package has no manifest origin: " + package.value()
            });
    }

    const auto asset_match = std::find_if(
        package_match->assets.begin(),
        package_match->assets.end(),
        [&asset](const assets::AssetDefinition& candidate) {
            return candidate.id == asset;
        });
    if (asset_match == package_match->assets.end()) {
        return core::Result<
            datasource::FileDataSource,
            PackageAssetSourceError>::failure(
            {
                PackageAssetSourceErrorCode::asset_not_found,
                package,
                asset,
                "Asset was not found: " + asset.value()
            });
    }

    auto package_root = package_set.manifest_paths[package_index].parent_path();
    if (package_root.empty()) {
        package_root = ".";
    }
    const assets::AssetLocator locator(filesystem_);
    const auto location = locator.resolve(package_root, *asset_match);
    if (!location.has_value()) {
        return core::Result<
            datasource::FileDataSource,
            PackageAssetSourceError>::failure(
            {
                PackageAssetSourceErrorCode::asset_location_error,
                package,
                asset,
                location.error().message
            });
    }

    auto source = datasource::FileDataSource::open(location.value());
    if (!source.has_value()) {
        return core::Result<
            datasource::FileDataSource,
            PackageAssetSourceError>::failure(
            {
                PackageAssetSourceErrorCode::source_open_error,
                package,
                asset,
                source.error().message
            });
    }
    return core::Result<
        datasource::FileDataSource,
        PackageAssetSourceError>::success(
        std::move(source.value()));
}

}

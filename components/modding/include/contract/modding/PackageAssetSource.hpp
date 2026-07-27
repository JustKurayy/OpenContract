#pragma once

#include <contract/assets/Asset.hpp>
#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/modding/PackageSet.hpp>

#include <string>

namespace contract::modding {

enum class PackageAssetSourceErrorCode {
    package_not_found,
    asset_not_found,
    manifest_origin_missing,
    asset_location_error,
    source_open_error
};

struct PackageAssetSourceError {
    PackageAssetSourceErrorCode code{
        PackageAssetSourceErrorCode::package_not_found};
    ModPackageId package;
    assets::AssetId asset;
    std::string message;
};

class PackageAssetSource {
public:
    explicit PackageAssetSource(
        const filesystem::IReadOnlyFilesystem& filesystem);

    [[nodiscard]] core::Result<
        datasource::FileDataSource,
        PackageAssetSourceError>
    open(
        const LoadedPackageSet& package_set,
        const ModPackageId& package,
        const assets::AssetId& asset) const;

private:
    const filesystem::IReadOnlyFilesystem& filesystem_;
};

}

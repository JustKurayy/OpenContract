#pragma once

#include <contract/core/Result.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/modding/ModManifest.hpp>
#include <contract/modding/PackageResolver.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace contract::modding {

enum class PackageSetErrorCode {
    manifest_error,
    resolution_error
};

struct PackageSetError {
    PackageSetErrorCode code{PackageSetErrorCode::manifest_error};
    std::filesystem::path path;
    std::string message;
};

struct LoadedPackageSet {
    std::vector<ModPackage> packages;
    std::vector<ModPackageId> load_order;
    std::vector<std::filesystem::path> manifest_paths;
};

class PackageSetLoader {
public:
    [[nodiscard]] core::Result<LoadedPackageSet, PackageSetError> load(
        std::span<const std::filesystem::path> paths,
        std::size_t maximum_manifest_size = default_manifest_size_limit) const;
};

enum class PackageAssetIssueCode {
    missing_manifest_origin,
    asset_location_error
};

struct PackageAssetIssue {
    PackageAssetIssueCode code{PackageAssetIssueCode::asset_location_error};
    ModPackageId package;
    assets::AssetId asset;
    std::string message;
};

class PackageAssetVerifier {
public:
    explicit PackageAssetVerifier(
        const filesystem::IReadOnlyFilesystem& filesystem);

    [[nodiscard]] std::vector<PackageAssetIssue> verify(
        const LoadedPackageSet& package_set) const;

private:
    const filesystem::IReadOnlyFilesystem& filesystem_;
};

}

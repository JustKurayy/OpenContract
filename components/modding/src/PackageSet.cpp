#include <contract/modding/PackageSet.hpp>

#include <contract/assets/AssetLocator.hpp>

#include <cstddef>
#include <utility>

namespace contract::modding {

core::Result<LoadedPackageSet, PackageSetError> PackageSetLoader::load(
    std::span<const std::filesystem::path> paths,
    std::size_t maximum_manifest_size) const {
    LoadedPackageSet loaded;
    loaded.packages.reserve(paths.size());

    const ModManifestCodec codec;
    for (const auto& path : paths) {
        auto package = codec.parse_file(path, maximum_manifest_size);
        if (!package) {
            return core::Result<LoadedPackageSet, PackageSetError>::failure(
                {
                    PackageSetErrorCode::manifest_error,
                    path,
                    package.error().message
                });
        }
        loaded.packages.push_back(std::move(package.value()));
        loaded.manifest_paths.push_back(path);
    }

    const PackageResolver resolver;
    auto resolution = resolver.resolve(loaded.packages);
    if (!resolution.valid()) {
        return core::Result<LoadedPackageSet, PackageSetError>::failure(
            {
                PackageSetErrorCode::resolution_error,
                {},
                resolution.issues.front().message
            });
    }
    loaded.load_order = std::move(resolution.load_order);
    return core::Result<LoadedPackageSet, PackageSetError>::success(
        std::move(loaded));
}

PackageAssetVerifier::PackageAssetVerifier(
    const filesystem::IReadOnlyFilesystem& filesystem)
    : filesystem_(filesystem) {}

std::vector<PackageAssetIssue> PackageAssetVerifier::verify(
    const LoadedPackageSet& package_set) const {
    std::vector<PackageAssetIssue> issues;
    const assets::AssetLocator locator(filesystem_);

    for (std::size_t index = 0; index < package_set.packages.size(); ++index) {
        const auto& package = package_set.packages[index];
        if (index >= package_set.manifest_paths.size()) {
            issues.push_back(
                {
                    PackageAssetIssueCode::missing_manifest_origin,
                    package.id,
                    {},
                    "Loaded package has no manifest origin"
                });
            continue;
        }

        auto package_root = package_set.manifest_paths[index].parent_path();
        if (package_root.empty()) {
            package_root = ".";
        }
        for (const auto& asset : package.assets) {
            const auto resolved = locator.resolve(
                package_root,
                asset);
            if (!resolved.has_value()) {
                issues.push_back(
                    {
                        PackageAssetIssueCode::asset_location_error,
                        package.id,
                        asset.id,
                        resolved.error().message
                    });
            }
        }
    }
    return issues;
}

}

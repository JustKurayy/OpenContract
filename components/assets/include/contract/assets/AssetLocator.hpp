#pragma once

#include <contract/assets/Asset.hpp>
#include <contract/core/Result.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>

#include <filesystem>
#include <string>

namespace contract::assets {

enum class AssetLocationErrorCode {
    invalid_source,
    package_root_error,
    source_error,
    source_outside_package
};

struct AssetLocationError {
    AssetLocationErrorCode code{AssetLocationErrorCode::invalid_source};
    std::string message;
};

class AssetLocator {
public:
    explicit AssetLocator(const filesystem::IReadOnlyFilesystem& filesystem);

    [[nodiscard]] core::Result<std::filesystem::path, AssetLocationError> resolve(
        const std::filesystem::path& package_root,
        const AssetDefinition& asset) const;

private:
    const filesystem::IReadOnlyFilesystem& filesystem_;
};

}

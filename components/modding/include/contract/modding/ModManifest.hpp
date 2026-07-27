#pragma once

#include <contract/core/Result.hpp>
#include <contract/modding/ModPackage.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace contract::modding {

inline constexpr std::uint32_t mod_manifest_schema_version = 1;
inline constexpr std::size_t default_manifest_size_limit = 1024U * 1024U;

enum class ManifestErrorCode {
    source_error,
    size_limit_exceeded,
    syntax_error,
    schema_error,
    unsupported_version,
    validation_error
};

struct ManifestError {
    ManifestErrorCode code{ManifestErrorCode::schema_error};
    std::string message;
    std::optional<std::size_t> byte_offset;
};

class ModManifestCodec {
public:
    [[nodiscard]] core::Result<std::string, ManifestError> serialize(
        const ModPackage& package) const;

    [[nodiscard]] core::Result<ModPackage, ManifestError> parse(
        std::string_view input,
        std::size_t maximum_size = default_manifest_size_limit) const;

    [[nodiscard]] core::Result<ModPackage, ManifestError> parse_file(
        const std::filesystem::path& path,
        std::size_t maximum_size = default_manifest_size_limit) const;
};

}

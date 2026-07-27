#include <contract/assets/AssetLocator.hpp>

#include <algorithm>
#include <cwctype>

namespace contract::assets {
namespace {

bool is_safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() ||
        path.has_root_directory()) {
        return false;
    }
    return std::ranges::none_of(path, [](const std::filesystem::path& part) {
        return part == ".." || part == ".";
    });
}

bool component_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
#ifdef _WIN32
    auto left_value = left.native();
    auto right_value = right.native();
    std::ranges::transform(
        left_value,
        left_value.begin(),
        [](wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    std::ranges::transform(
        right_value,
        right_value.begin(),
        [](wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    return left_value == right_value;
#else
    return left.native() == right.native();
#endif
}

bool is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    while (root_part != root.end()) {
        if (candidate_part == candidate.end() ||
            !component_equal(*root_part, *candidate_part)) {
            return false;
        }
        ++root_part;
        ++candidate_part;
    }
    return true;
}

}

AssetLocator::AssetLocator(const filesystem::IReadOnlyFilesystem& filesystem)
    : filesystem_(filesystem) {}

core::Result<std::filesystem::path, AssetLocationError> AssetLocator::resolve(
    const std::filesystem::path& package_root,
    const AssetDefinition& asset) const {
    if (!is_safe_relative_path(asset.source)) {
        return core::Result<std::filesystem::path, AssetLocationError>::failure(
            {
                AssetLocationErrorCode::invalid_source,
                "Asset source must be a safe relative path"
            });
    }

    const auto canonical_root = filesystem_.canonicalize(package_root);
    if (!canonical_root.has_value()) {
        return core::Result<std::filesystem::path, AssetLocationError>::failure(
            {
                AssetLocationErrorCode::package_root_error,
                canonical_root.error().message
            });
    }

    const auto canonical_source = filesystem_.canonicalize(
        canonical_root.value() / asset.source);
    if (!canonical_source.has_value()) {
        return core::Result<std::filesystem::path, AssetLocationError>::failure(
            {
                AssetLocationErrorCode::source_error,
                canonical_source.error().message
            });
    }
    if (!is_within(canonical_root.value(), canonical_source.value())) {
        return core::Result<std::filesystem::path, AssetLocationError>::failure(
            {
                AssetLocationErrorCode::source_outside_package,
                "Canonical asset source is outside the package root"
            });
    }

    return core::Result<std::filesystem::path, AssetLocationError>::success(
        canonical_source.value());
}

}

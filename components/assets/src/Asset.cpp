#include <contract/assets/Asset.hpp>

#include <unordered_set>

namespace contract::assets {
namespace {

bool is_safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() ||
        path.has_root_directory()) {
        return false;
    }

    for (const auto& part : path) {
        if (part == ".." || part == ".") {
            return false;
        }
    }
    return true;
}

}

std::vector<AssetIssue> AssetCatalogValidator::validate(
    const std::vector<AssetDefinition>& assets) const {
    std::vector<AssetIssue> issues;
    std::unordered_set<std::string> identifiers;

    for (const auto& asset : assets) {
        if (!asset.id.valid()) {
            issues.push_back(
                {AssetIssueCode::invalid_identifier, "Asset identifier is empty"});
        } else if (!identifiers.insert(asset.id.value()).second) {
            issues.push_back(
                {AssetIssueCode::duplicate_identifier,
                 "Duplicate asset identifier: " + asset.id.value()});
        }

        if (!is_safe_relative_path(asset.source)) {
            issues.push_back(
                {AssetIssueCode::invalid_source_path,
                 "Asset source must be a non-empty relative path without parent traversal"});
        }
    }

    return issues;
}

}

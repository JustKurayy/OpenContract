#pragma once

#include <contract/core/Identifier.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace contract::assets {

using AssetId = core::Identifier<struct AssetIdTag>;

struct AssetDefinition {
    AssetId id;
    std::filesystem::path source;
};

enum class AssetIssueCode {
    invalid_identifier,
    invalid_source_path,
    duplicate_identifier
};

struct AssetIssue {
    AssetIssueCode code{AssetIssueCode::invalid_identifier};
    std::string message;
};

class AssetCatalogValidator {
public:
    [[nodiscard]] std::vector<AssetIssue> validate(
        const std::vector<AssetDefinition>& assets) const;
};

}

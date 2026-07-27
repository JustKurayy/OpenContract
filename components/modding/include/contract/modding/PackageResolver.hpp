#pragma once

#include <contract/modding/ModPackage.hpp>

#include <span>
#include <string>
#include <vector>

namespace contract::modding {

enum class PackageResolutionIssueCode {
    invalid_package,
    duplicate_package,
    missing_dependency,
    dependency_version_too_low,
    dependency_cycle
};

struct PackageResolutionIssue {
    PackageResolutionIssueCode code{PackageResolutionIssueCode::invalid_package};
    std::string package;
    std::string dependency;
    std::string message;
};

struct PackageResolution {
    std::vector<ModPackageId> load_order;
    std::vector<PackageResolutionIssue> issues;

    [[nodiscard]] bool valid() const noexcept {
        return issues.empty();
    }
};

class PackageResolver {
public:
    [[nodiscard]] PackageResolution resolve(
        std::span<const ModPackage> packages) const;
};

}

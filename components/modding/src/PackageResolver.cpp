#include <contract/modding/PackageResolver.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace contract::modding {

PackageResolution PackageResolver::resolve(
    std::span<const ModPackage> packages) const {
    PackageResolution resolution;
    std::unordered_map<std::string, const ModPackage*> catalog;
    const ModPackageValidator validator;

    for (const auto& package : packages) {
        const auto validation_issues = validator.validate(package);
        if (!validation_issues.empty()) {
            resolution.issues.push_back(
                {
                    PackageResolutionIssueCode::invalid_package,
                    package.id.value(),
                    {},
                    validation_issues.front().message
                });
        }
        if (!package.id.valid()) {
            continue;
        }
        if (!catalog.try_emplace(package.id.value(), &package).second) {
            resolution.issues.push_back(
                {
                    PackageResolutionIssueCode::duplicate_package,
                    package.id.value(),
                    {},
                    "Package identifier is declared more than once"
                });
        }
    }

    for (const auto& [package_id, package] : catalog) {
        for (const auto& dependency : package->dependencies) {
            const auto found = catalog.find(dependency.package.value());
            if (found == catalog.end()) {
                resolution.issues.push_back(
                    {
                        PackageResolutionIssueCode::missing_dependency,
                        package_id,
                        dependency.package.value(),
                        "Required package dependency is missing"
                    });
                continue;
            }
            if (found->second->version < dependency.minimum_version) {
                resolution.issues.push_back(
                    {
                        PackageResolutionIssueCode::dependency_version_too_low,
                        package_id,
                        dependency.package.value(),
                        "Installed package dependency does not satisfy the minimum version"
                    });
            }
        }
    }

    if (!resolution.issues.empty()) {
        return resolution;
    }

    std::vector<std::string> package_ids;
    package_ids.reserve(catalog.size());
    for (const auto& [package_id, package] : catalog) {
        static_cast<void>(package);
        package_ids.push_back(package_id);
    }
    std::ranges::sort(package_ids);

    enum class VisitState {
        unvisited,
        visiting,
        visited
    };
    std::unordered_map<std::string, VisitState> states;

    std::function<bool(const std::string&)> visit =
        [&](const std::string& package_id) {
            const auto state = states.find(package_id);
            if (state != states.end() && state->second == VisitState::visiting) {
                resolution.issues.push_back(
                    {
                        PackageResolutionIssueCode::dependency_cycle,
                        package_id,
                        package_id,
                        "Package dependency cycle detected"
                    });
                return false;
            }
            if (state != states.end() && state->second == VisitState::visited) {
                return true;
            }

            states[package_id] = VisitState::visiting;
            std::vector<std::string> dependencies;
            for (const auto& dependency : catalog.at(package_id)->dependencies) {
                dependencies.push_back(dependency.package.value());
            }
            std::ranges::sort(dependencies);
            for (const auto& dependency : dependencies) {
                const auto dependency_state = states.find(dependency);
                if (dependency_state != states.end() &&
                    dependency_state->second == VisitState::visiting) {
                    resolution.issues.push_back(
                        {
                            PackageResolutionIssueCode::dependency_cycle,
                            package_id,
                            dependency,
                            "Package dependency cycle detected"
                        });
                    return false;
                }
                if (!visit(dependency)) {
                    return false;
                }
            }
            states[package_id] = VisitState::visited;
            resolution.load_order.emplace_back(package_id);
            return true;
        };

    for (const auto& package_id : package_ids) {
        if (!visit(package_id)) {
            resolution.load_order.clear();
            break;
        }
    }
    return resolution;
}

}

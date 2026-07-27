#include "TestSupport.hpp"

#include <contract/modding/PackageResolver.hpp>

#include <string>
#include <vector>

namespace {

contract::modding::ModPackage package(
    std::string id,
    contract::modding::SemanticVersion version,
    std::vector<contract::modding::PackageDependency> dependencies = {}) {
    return {
        contract::modding::ModPackageId(std::move(id)),
        version,
        {"Synthetic", "Synthetic Author", "Synthetic only"},
        std::move(dependencies),
        {},
        {},
        {},
        {}};
}

const contract::modding::PackageResolutionIssue* find_issue(
    const contract::modding::PackageResolution& resolution,
    contract::modding::PackageResolutionIssueCode code) {
    for (const auto& issue : resolution.issues) {
        if (issue.code == code) {
            return &issue;
        }
    }
    return nullptr;
}

}

int main() {
    using namespace contract::modding;

    const auto base = package("package.base", {1, 2, 0});
    const auto addon = package(
        "package.addon",
        {2, 0, 0},
        {
            PackageDependency{
                ModPackageId("package.base"),
                SemanticVersion{1, 1, 0}}
        });
    const auto expansion = package(
        "package.expansion",
        {1, 0, 0},
        {
            PackageDependency{
                ModPackageId("package.addon"),
                SemanticVersion{2, 0, 0}}
        });

    PackageResolver resolver;
    const std::vector packages{expansion, addon, base};
    const auto resolution = resolver.resolve(packages);
    CONTRACT_EXPECT(resolution.valid());
    CONTRACT_EXPECT_EQ(resolution.load_order.size(), std::size_t{3});
    CONTRACT_EXPECT_EQ(
        resolution.load_order[0].value(),
        std::string("package.base"));
    CONTRACT_EXPECT_EQ(
        resolution.load_order[1].value(),
        std::string("package.addon"));
    CONTRACT_EXPECT_EQ(
        resolution.load_order[2].value(),
        std::string("package.expansion"));

    const auto missing = package(
        "package.missing-user",
        {1, 0, 0},
        {
            PackageDependency{
                ModPackageId("package.not-installed"),
                SemanticVersion{1, 0, 0}}
        });
    const std::vector missing_packages{missing};
    const auto missing_resolution = resolver.resolve(missing_packages);
    CONTRACT_EXPECT(!missing_resolution.valid());
    CONTRACT_EXPECT(
        find_issue(
            missing_resolution,
            PackageResolutionIssueCode::missing_dependency) != nullptr);

    const auto old_base = package("package.base", {1, 0, 0});
    const auto demanding_addon = package(
        "package.demanding",
        {1, 0, 0},
        {
            PackageDependency{
                ModPackageId("package.base"),
                SemanticVersion{2, 0, 0}}
        });
    const std::vector old_packages{demanding_addon, old_base};
    const auto version_resolution = resolver.resolve(old_packages);
    CONTRACT_EXPECT(
        find_issue(
            version_resolution,
            PackageResolutionIssueCode::dependency_version_too_low) != nullptr);

    const std::vector duplicate_packages{base, base};
    const auto duplicate_resolution = resolver.resolve(duplicate_packages);
    CONTRACT_EXPECT(
        find_issue(
            duplicate_resolution,
            PackageResolutionIssueCode::duplicate_package) != nullptr);

    const auto cycle_a = package(
        "package.a",
        {1, 0, 0},
        {
            PackageDependency{
                ModPackageId("package.b"),
                SemanticVersion{1, 0, 0}}
        });
    const auto cycle_b = package(
        "package.b",
        {1, 0, 0},
        {
            PackageDependency{
                ModPackageId("package.a"),
                SemanticVersion{1, 0, 0}}
        });
    const std::vector cycle_packages{cycle_b, cycle_a};
    const auto cycle_resolution = resolver.resolve(cycle_packages);
    const auto* cycle_issue = find_issue(
        cycle_resolution,
        PackageResolutionIssueCode::dependency_cycle);
    CONTRACT_EXPECT(cycle_issue != nullptr);
    CONTRACT_EXPECT(cycle_resolution.load_order.empty());
    if (cycle_issue != nullptr) {
        CONTRACT_EXPECT_EQ(cycle_issue->package, std::string("package.b"));
        CONTRACT_EXPECT_EQ(cycle_issue->dependency, std::string("package.a"));
    }

    return contract::test::finish();
}

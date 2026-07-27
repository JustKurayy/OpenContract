#include "TestSupport.hpp"

#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/installation/Installation.hpp>

#include <filesystem>

int main() {
    const auto configured_path =
        contract::installation::configured_game_path();
    CONTRACT_EXPECT(configured_path.has_value());
    if (!configured_path.has_value()) {
        return contract::test::finish();
    }

    contract::filesystem::NativeReadOnlyFilesystem filesystem;
    contract::installation::InstallationValidator validator(
        filesystem,
        contract::installation::default_recognition_policy());
    const auto report = validator.validate(*configured_path);
    CONTRACT_EXPECT(report.structural_validation_only);
    CONTRACT_EXPECT_EQ(
        report.status,
        contract::installation::InstallationStatus::plausible_installation);

    return contract::test::finish();
}

#include "TestSupport.hpp"

#include <contract/installation/Installation.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using contract::core::Result;
using contract::filesystem::DirectoryEntry;
using contract::filesystem::EntryType;
using contract::filesystem::FilesystemError;
using contract::filesystem::FilesystemErrorCode;
using contract::filesystem::IReadOnlyFilesystem;
using contract::filesystem::ReadOnlyBinaryFile;

class SyntheticFilesystem final : public IReadOnlyFilesystem {
public:
    Result<std::filesystem::path, FilesystemError> canonicalize(
        const std::filesystem::path& path) const override {
        if (error.has_value()) {
            return Result<std::filesystem::path, FilesystemError>::failure(*error);
        }
        return Result<std::filesystem::path, FilesystemError>::success(path.lexically_normal());
    }

    Result<std::vector<DirectoryEntry>, FilesystemError> enumerate_top_level(
        const std::filesystem::path&) const override {
        if (error.has_value()) {
            return Result<std::vector<DirectoryEntry>, FilesystemError>::failure(*error);
        }
        return Result<std::vector<DirectoryEntry>, FilesystemError>::success(entries);
    }

    Result<ReadOnlyBinaryFile, FilesystemError> read_binary_file(
        const std::filesystem::path&,
        std::size_t) const override {
        return Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::io_error, "not used"});
    }

    std::optional<FilesystemError> error;
    std::vector<DirectoryEntry> entries;
};

DirectoryEntry file(std::string name) {
    return {{}, std::move(name), EntryType::file, 100};
}

DirectoryEntry directory(std::string name) {
    return {{}, std::move(name), EntryType::directory, 0};
}

}

int main() {
    using contract::installation::InstallationStatus;
    using contract::installation::RecognitionPolicy;

    const RecognitionPolicy policy{
        {"engine.bin", "content.pack"},
        {"levels"},
        3};

    SyntheticFilesystem filesystem;
    filesystem.entries = {
        directory("levels"),
        file("content.pack"),
        file("engine.bin")};

    contract::installation::InstallationValidator validator(filesystem, policy);
    const auto plausible = validator.validate("C:/synthetic-game");
    CONTRACT_EXPECT_EQ(plausible.status, InstallationStatus::plausible_installation);
    CONTRACT_EXPECT(plausible.structural_validation_only);
    CONTRACT_EXPECT_EQ(plausible.entries.size(), std::size_t{3});

    filesystem.entries = {file("unrelated.bin")};
    const auto unrecognized = validator.validate("C:/synthetic-game");
    CONTRACT_EXPECT_EQ(unrecognized.status, InstallationStatus::unrecognized);

    filesystem.error = FilesystemError{FilesystemErrorCode::path_missing, "missing"};
    CONTRACT_EXPECT_EQ(
        validator.validate("C:/missing").status,
        InstallationStatus::path_missing);

    filesystem.error = FilesystemError{FilesystemErrorCode::not_directory, "not a directory"};
    CONTRACT_EXPECT_EQ(
        validator.validate("C:/synthetic-file").status,
        InstallationStatus::not_a_directory);

    filesystem.error = FilesystemError{FilesystemErrorCode::permission_denied, "denied"};
    CONTRACT_EXPECT_EQ(
        validator.validate("C:/denied").status,
        InstallationStatus::permission_denied);

    filesystem.error.reset();
    filesystem.entries = {directory("levels"), file("content.pack"), file("engine.bin")};
    const std::vector<std::filesystem::path> probes = {
        "C:/absent",
        "C:/synthetic-game"};
    const auto selected_explicit = contract::installation::select_game_path(
        std::filesystem::path("C:/explicit"),
        std::filesystem::path("C:/environment"),
        std::filesystem::path("C:/configured"),
        probes,
        validator);
    CONTRACT_EXPECT_EQ(selected_explicit.value(), std::filesystem::path("C:/explicit"));

    const auto selected_environment = contract::installation::select_game_path(
        std::nullopt,
        std::filesystem::path("C:/environment"),
        std::filesystem::path("C:/configured"),
        probes,
        validator);
    CONTRACT_EXPECT_EQ(selected_environment.value(), std::filesystem::path("C:/environment"));

    const auto selected_configured = contract::installation::select_game_path(
        std::nullopt,
        std::nullopt,
        std::filesystem::path("C:/configured"),
        probes,
        validator);
    CONTRACT_EXPECT_EQ(selected_configured.value(), std::filesystem::path("C:/configured"));

    const auto selected_probe = contract::installation::select_game_path(
        std::nullopt,
        std::nullopt,
        std::nullopt,
        probes,
        validator);
    CONTRACT_EXPECT_EQ(selected_probe.value(), std::filesystem::path("C:/absent"));

    return contract::test::finish();
}

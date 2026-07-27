#pragma once

#include <contract/diagnostics/Diagnostic.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace contract::installation {

enum class InstallationStatus {
    path_missing,
    not_a_directory,
    permission_denied,
    unrecognized,
    plausible_installation
};

struct RecognitionPolicy {
    std::vector<std::string> file_markers;
    std::vector<std::string> directory_markers;
    std::size_t minimum_matches{1};
};

struct ValidationReport {
    std::filesystem::path requested_path;
    std::optional<std::filesystem::path> canonical_path;
    InstallationStatus status{InstallationStatus::path_missing};
    bool structural_validation_only{true};
    std::size_t marker_matches{0};
    std::vector<filesystem::DirectoryEntry> entries;
    std::vector<diagnostics::Diagnostic> diagnostics;
};

class InstallationValidator {
public:
    InstallationValidator(
        const filesystem::IReadOnlyFilesystem& filesystem,
        RecognitionPolicy policy);

    [[nodiscard]] ValidationReport validate(const std::filesystem::path& path) const;

private:
    const filesystem::IReadOnlyFilesystem& filesystem_;
    RecognitionPolicy policy_;
};

[[nodiscard]] RecognitionPolicy default_recognition_policy();
[[nodiscard]] std::vector<std::filesystem::path> default_windows_probe_paths();
[[nodiscard]] std::optional<std::filesystem::path> environment_game_path();
[[nodiscard]] std::optional<std::filesystem::path> configured_game_path();

[[nodiscard]] std::optional<std::filesystem::path> select_game_path(
    const std::optional<std::filesystem::path>& explicit_path,
    const std::optional<std::filesystem::path>& environment_path,
    const std::optional<std::filesystem::path>& configured_path,
    const std::vector<std::filesystem::path>& probe_paths,
    const InstallationValidator& validator);

[[nodiscard]] std::string status_name(InstallationStatus status);

}

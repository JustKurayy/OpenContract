#include <contract/installation/Installation.hpp>

#include <contract/installation/BuildConfiguration.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace contract::installation {
namespace {

std::string normalized_name(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

InstallationStatus status_from_error(filesystem::FilesystemErrorCode code) {
    switch (code) {
    case filesystem::FilesystemErrorCode::path_missing:
        return InstallationStatus::path_missing;
    case filesystem::FilesystemErrorCode::not_directory:
    case filesystem::FilesystemErrorCode::not_a_file:
        return InstallationStatus::not_a_directory;
    case filesystem::FilesystemErrorCode::permission_denied:
        return InstallationStatus::permission_denied;
    case filesystem::FilesystemErrorCode::size_limit_exceeded:
    case filesystem::FilesystemErrorCode::overflow:
    case filesystem::FilesystemErrorCode::io_error:
    case filesystem::FilesystemErrorCode::invalid_view:
        return InstallationStatus::permission_denied;
    }
    return InstallationStatus::permission_denied;
}

diagnostics::Diagnostic validation_diagnostic(
    diagnostics::Severity severity,
    std::string code,
    std::string message,
    const std::filesystem::path& path) {
    return {severity, std::move(code), std::move(message), path, std::nullopt};
}

}

InstallationValidator::InstallationValidator(
    const filesystem::IReadOnlyFilesystem& filesystem,
    RecognitionPolicy policy)
    : filesystem_(filesystem), policy_(std::move(policy)) {}

ValidationReport InstallationValidator::validate(const std::filesystem::path& path) const {
    ValidationReport report;
    report.requested_path = path;

    const auto canonical = filesystem_.canonicalize(path);
    if (!canonical) {
        report.status = status_from_error(canonical.error().code);
        report.diagnostics.push_back(validation_diagnostic(
            diagnostics::Severity::error,
            "installation.path",
            canonical.error().message,
            path));
        return report;
    }
    report.canonical_path = canonical.value();

    const auto entries = filesystem_.enumerate_top_level(canonical.value());
    if (!entries) {
        report.status = status_from_error(entries.error().code);
        report.diagnostics.push_back(validation_diagnostic(
            diagnostics::Severity::error,
            "installation.enumeration",
            entries.error().message,
            canonical.value()));
        return report;
    }
    report.entries = entries.value();

    std::unordered_set<std::string> files;
    std::unordered_set<std::string> directories;
    for (const auto& entry : report.entries) {
        if (entry.type == filesystem::EntryType::file) {
            files.insert(normalized_name(entry.name));
        } else if (entry.type == filesystem::EntryType::directory) {
            directories.insert(normalized_name(entry.name));
        }
    }

    for (const auto& marker : policy_.file_markers) {
        if (files.contains(normalized_name(marker))) {
            ++report.marker_matches;
        }
    }
    for (const auto& marker : policy_.directory_markers) {
        if (directories.contains(normalized_name(marker))) {
            ++report.marker_matches;
        }
    }

    if (report.marker_matches >= policy_.minimum_matches) {
        report.status = InstallationStatus::plausible_installation;
        report.diagnostics.push_back(validation_diagnostic(
            diagnostics::Severity::information,
            "installation.structural-plausible",
            "Directory passed structural validation only; ownership and licensing were not verified",
            canonical.value()));
    } else {
        report.status = InstallationStatus::unrecognized;
        report.diagnostics.push_back(validation_diagnostic(
            diagnostics::Severity::warning,
            "installation.unrecognized",
            "Directory is readable but does not satisfy the configured structural markers",
            canonical.value()));
    }
    return report;
}

RecognitionPolicy default_recognition_policy() {
    return {
        {"HitmanBloodMoney.exe", "pc_eng.str", "InstallScript.vdf"},
        {"Movies", "Scenes", "Scriptcs"},
        4};
}

std::vector<std::filesystem::path> default_windows_probe_paths() {
    return {
        R"(C:\Program Files (x86)\Steam\steamapps\common\Hitman Blood Money)"};
}

std::optional<std::filesystem::path> environment_game_path() {
#ifdef _WIN32
    char* raw_value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&raw_value, &length, "CONTRACT_GAME_PATH") != 0) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> value(raw_value, &std::free);
    if (value == nullptr || length <= 1) {
        return std::nullopt;
    }
    return std::filesystem::path(value.get());
#else
    const char* value = std::getenv("CONTRACT_GAME_PATH");
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(value);
#endif
}

std::optional<std::filesystem::path> configured_game_path() {
    const std::string_view value(CONTRACT_CONFIGURED_GAME_PATH_VALUE);
    if (value.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

std::optional<std::filesystem::path> select_game_path(
    const std::optional<std::filesystem::path>& explicit_path,
    const std::optional<std::filesystem::path>& environment_path,
    const std::optional<std::filesystem::path>& configured_path,
    const std::vector<std::filesystem::path>& probe_paths,
    const InstallationValidator& validator) {
    if (explicit_path.has_value()) {
        return explicit_path;
    }
    if (environment_path.has_value()) {
        return environment_path;
    }
    if (configured_path.has_value()) {
        return configured_path;
    }
    for (const auto& candidate : probe_paths) {
        if (validator.validate(candidate).status == InstallationStatus::plausible_installation) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string status_name(InstallationStatus status) {
    switch (status) {
    case InstallationStatus::path_missing:
        return "path_missing";
    case InstallationStatus::not_a_directory:
        return "not_a_directory";
    case InstallationStatus::permission_denied:
        return "permission_denied";
    case InstallationStatus::unrecognized:
        return "unrecognized";
    case InstallationStatus::plausible_installation:
        return "plausible_installation";
    }
    return "unknown";
}

}

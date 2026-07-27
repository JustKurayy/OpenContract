#pragma once

#include <contract/diagnostics/DiagnosticSink.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/installation/Installation.hpp>

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace contract::runtime {

struct RuntimeOptions {
    std::optional<std::filesystem::path> game_path;
    std::vector<std::filesystem::path> mod_manifests;
    std::optional<std::string> mission;
    bool help{false};
};

struct RuntimeContext {
    const filesystem::IReadOnlyFilesystem& filesystem;
    diagnostics::IDiagnosticSink& diagnostics;
    installation::RecognitionPolicy recognition_policy;
    std::optional<std::filesystem::path> environment_path;
    std::optional<std::filesystem::path> configured_path;
    std::vector<std::filesystem::path> probe_paths;
};

enum class RuntimeExitCode : int {
    success = 0,
    usage_error = 2,
    game_path_missing = 3,
    installation_invalid = 4,
    mod_set_invalid = 5,
    mission_invalid = 6,
    session_invalid = 7,
    not_implemented = 10
};

[[nodiscard]] std::optional<RuntimeOptions> parse_runtime_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors);

void print_runtime_help(std::ostream& output);

[[nodiscard]] int run_runtime(
    const RuntimeOptions& options,
    const RuntimeContext& context,
    std::ostream& output,
    std::ostream& errors);

[[nodiscard]] int run_runtime(
    const RuntimeOptions& options,
    std::ostream& output,
    std::ostream& errors);

}

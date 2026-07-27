#pragma once

#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/installation/Installation.hpp>

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace contract::tools {

struct InspectorOptions {
    std::optional<std::filesystem::path> game_path;
    bool json{false};
    bool help{false};
};

struct InspectorContext {
    const filesystem::IReadOnlyFilesystem& filesystem;
    installation::RecognitionPolicy recognition_policy;
    std::optional<std::filesystem::path> environment_path;
    std::optional<std::filesystem::path> configured_path;
    std::vector<std::filesystem::path> probe_paths;
};

enum class InspectorExitCode : int {
    success = 0,
    usage_error = 2,
    path_missing = 3,
    not_a_directory = 4,
    permission_denied = 5,
    unrecognized = 6
};

[[nodiscard]] std::optional<InspectorOptions> parse_inspector_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors);

[[nodiscard]] int run_inspector(
    const InspectorOptions& options,
    std::ostream& output,
    std::ostream& errors);

[[nodiscard]] int run_inspector(
    const InspectorOptions& options,
    const InspectorContext& context,
    std::ostream& output,
    std::ostream& errors);

void print_inspector_help(std::ostream& output);

}

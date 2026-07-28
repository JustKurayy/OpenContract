#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace contract::tools {

struct ArchiveInspectorOptions {
    std::optional<std::filesystem::path> archive_path;
    bool json{false};
    bool help{false};
};

enum class ArchiveInspectorExitCode : int {
    success = 0,
    usage_error = 2,
    source_error = 3,
    invalid_archive = 4
};

[[nodiscard]] std::optional<ArchiveInspectorOptions>
parse_archive_inspector_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors);

[[nodiscard]] int run_archive_inspector(
    const ArchiveInspectorOptions& options,
    std::ostream& output,
    std::ostream& errors);

void print_archive_inspector_help(std::ostream& output);

}

#pragma once

#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/installation/Installation.hpp>

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace contract::launcher {

struct LauncherOptions {
    std::optional<std::filesystem::path> game_path;
    std::vector<std::filesystem::path> mod_manifests;
    std::optional<std::string> mission;
    bool run{false};
    bool help{false};
};

enum class LauncherExitCode : int {
    success = 0,
    usage_error = 2,
    game_path_missing = 3,
    installation_invalid = 4,
    invocation_failed = 7
};

class IRuntimeProcess {
public:
    virtual ~IRuntimeProcess() = default;

    [[nodiscard]] virtual int invoke(
        const std::filesystem::path& game_path,
        const std::vector<std::filesystem::path>& mod_manifests,
        const std::optional<std::string>& mission) const = 0;
};

struct LauncherContext {
    const filesystem::IReadOnlyFilesystem& filesystem;
    installation::RecognitionPolicy recognition_policy;
    std::optional<std::filesystem::path> environment_path;
    std::optional<std::filesystem::path> configured_path;
    std::vector<std::filesystem::path> probe_paths;
    const IRuntimeProcess& runtime_process;
};

[[nodiscard]] std::optional<LauncherOptions> parse_launcher_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors);

void print_launcher_help(std::ostream& output);

[[nodiscard]] int run_launcher(
    const LauncherOptions& options,
    const LauncherContext& context,
    std::ostream& output,
    std::ostream& errors);

[[nodiscard]] std::wstring quote_windows_argument(std::wstring_view argument);

[[nodiscard]] std::wstring build_windows_command_line(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments);

}

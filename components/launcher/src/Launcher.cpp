#include <contract/launcher/Launcher.hpp>

#include <cstddef>
#include <ostream>

namespace contract::launcher {

std::optional<LauncherOptions> parse_launcher_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors) {
    LauncherOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--game-path") {
            if (index + 1 >= arguments.size()) {
                errors << "--game-path requires a path\n";
                return std::nullopt;
            }
            options.game_path = std::filesystem::path(arguments[++index]);
        } else if (argument == "--mod-manifest") {
            if (index + 1 >= arguments.size()) {
                errors << "--mod-manifest requires a path\n";
                return std::nullopt;
            }
            options.mod_manifests.emplace_back(arguments[++index]);
        } else if (argument == "--mission") {
            if (index + 1 >= arguments.size()) {
                errors << "--mission requires an identifier\n";
                return std::nullopt;
            }
            options.mission = arguments[++index];
        } else if (argument == "--run") {
            options.run = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            errors << "Unknown option: " << argument << '\n';
            return std::nullopt;
        }
    }
    return options;
}

void print_launcher_help(std::ostream& output) {
    output
        << "Usage: contract-launcher [--game-path <directory>]"
        << " [--mod-manifest <file>]... [--mission <identifier>]"
        << " [--run] [--help]\n"
        << "Validates a directory and optionally starts contract-runtime.\n";
}

int run_launcher(
    const LauncherOptions& options,
    const LauncherContext& context,
    std::ostream& output,
    std::ostream& errors) {
    const installation::InstallationValidator validator(
        context.filesystem,
        context.recognition_policy);
    const auto selected_path = installation::select_game_path(
        options.game_path,
        context.environment_path,
        context.configured_path,
        context.probe_paths,
        validator);
    if (!selected_path.has_value()) {
        errors << "No candidate game directory was supplied or discovered\n";
        return static_cast<int>(LauncherExitCode::game_path_missing);
    }

    const auto report = validator.validate(*selected_path);
    output << "Validation: "
           << installation::status_name(report.status)
           << " (structural validation only)\n";
    if (report.status != installation::InstallationStatus::plausible_installation) {
        return static_cast<int>(LauncherExitCode::installation_invalid);
    }
    if (!options.run) {
        return static_cast<int>(LauncherExitCode::success);
    }
    return context.runtime_process.invoke(
        *report.canonical_path,
        options.mod_manifests,
        options.mission);
}

std::wstring quote_windows_argument(std::wstring_view argument) {
    std::wstring quoted;
    quoted.push_back(L'"');

    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append((backslashes * 2U) + 1U, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
        }
        backslashes = 0;
    }

    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring build_windows_command_line(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments) {
    std::wstring command_line = quote_windows_argument(executable.wstring());
    for (const auto& argument : arguments) {
        command_line.push_back(L' ');
        command_line.append(quote_windows_argument(argument));
    }
    return command_line;
}

}

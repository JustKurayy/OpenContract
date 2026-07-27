#include <contract/tools/Inspector.hpp>

#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/formats/FormatProbe.hpp>
#include <contract/installation/Installation.hpp>

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

namespace contract::tools {
namespace {

std::string json_escape(std::string_view value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20U) {
                output << "\\u"
                       << std::hex
                       << std::setw(4)
                       << std::setfill('0')
                       << static_cast<unsigned int>(character)
                       << std::dec;
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    return output.str();
}

InspectorExitCode exit_code_for(installation::InstallationStatus status) {
    switch (status) {
    case installation::InstallationStatus::path_missing:
        return InspectorExitCode::path_missing;
    case installation::InstallationStatus::not_a_directory:
        return InspectorExitCode::not_a_directory;
    case installation::InstallationStatus::permission_denied:
        return InspectorExitCode::permission_denied;
    case installation::InstallationStatus::unrecognized:
        return InspectorExitCode::unrecognized;
    case installation::InstallationStatus::plausible_installation:
        return InspectorExitCode::success;
    }
    return InspectorExitCode::permission_denied;
}

std::string entry_type_name(filesystem::EntryType type) {
    switch (type) {
    case filesystem::EntryType::file:
        return "file";
    case filesystem::EntryType::directory:
        return "directory";
    case filesystem::EntryType::other:
        return "other";
    }
    return "other";
}

}

std::optional<InspectorOptions> parse_inspector_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors) {
    InspectorOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--game-path") {
            if (index + 1 >= arguments.size()) {
                errors << "--game-path requires a path\n";
                return std::nullopt;
            }
            options.game_path = std::filesystem::path(arguments[++index]);
        } else {
            errors << "Unknown option: " << argument << '\n';
            return std::nullopt;
        }
    }
    return options;
}

int run_inspector(
    const InspectorOptions& options,
    std::ostream& output,
    std::ostream& errors) {
    filesystem::NativeReadOnlyFilesystem filesystem;
    const InspectorContext context{
        filesystem,
        installation::default_recognition_policy(),
        installation::environment_game_path(),
        installation::configured_game_path(),
        installation::default_windows_probe_paths()};
    return run_inspector(options, context, output, errors);
}

int run_inspector(
    const InspectorOptions& options,
    const InspectorContext& context,
    std::ostream& output,
    std::ostream& errors) {
    installation::InstallationValidator validator(
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
        return static_cast<int>(InspectorExitCode::path_missing);
    }

    const auto report = validator.validate(*selected_path);
    const formats::FormatProbeResult unsupported{
        "unsupported",
        formats::FormatConfidence::none};

    if (options.json) {
        output << "{\"path\":\""
               << json_escape(
                      report.canonical_path.value_or(report.requested_path).string())
               << "\",\"validation\":\""
               << installation::status_name(report.status)
               << "\",\"structural_validation_only\":true,\"entries\":[";
        for (std::size_t index = 0; index < report.entries.size(); ++index) {
            const auto& entry = report.entries[index];
            if (index != 0) {
                output << ',';
            }
            output << "{\"name\":\""
                   << json_escape(entry.name)
                   << "\",\"type\":\""
                   << entry_type_name(entry.type)
                   << "\",\"size\":"
                   << entry.size
                   << ",\"probe\":\""
                   << json_escape(unsupported.format_name)
                   << "\"}";
        }
        output << "]}\n";
    } else {
        output << "Path: "
               << report.canonical_path.value_or(report.requested_path).string()
               << '\n'
               << "Validation: "
               << installation::status_name(report.status)
               << " (structural validation only)\n";
        for (const auto& entry : report.entries) {
            output << entry.name
                   << '\t'
                   << entry_type_name(entry.type)
                   << '\t'
                   << entry.size
                   << '\t'
                   << unsupported.format_name
                   << '\n';
        }
    }

    return static_cast<int>(exit_code_for(report.status));
}

void print_inspector_help(std::ostream& output) {
    output
        << "Usage: contract-inspect [--game-path <directory>] [--json] [--help]\n"
        << "Performs read-only structural validation and top-level metadata inspection.\n";
}

}

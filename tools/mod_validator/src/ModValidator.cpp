#include <contract/tools/ModValidator.hpp>

#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/modding/ModManifest.hpp>
#include <contract/modding/PackageSet.hpp>

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

}

std::optional<ModValidatorOptions> parse_mod_validator_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors) {
    ModValidatorOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--manifest") {
            if (index + 1 >= arguments.size()) {
                errors << "--manifest requires a path\n";
                return std::nullopt;
            }
            options.manifest_path = std::filesystem::path(arguments[++index]);
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--check-assets") {
            options.check_assets = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            errors << "Unknown option: " << argument << '\n';
            return std::nullopt;
        }
    }
    if (!options.help && !options.manifest_path.has_value()) {
        errors << "--manifest is required\n";
        return std::nullopt;
    }
    return options;
}

int run_mod_validator(
    const ModValidatorOptions& options,
    std::ostream& output,
    std::ostream& errors) {
    if (!options.manifest_path.has_value()) {
        errors << "No manifest path was supplied\n";
        return static_cast<int>(ModValidatorExitCode::usage_error);
    }

    const modding::ModManifestCodec codec;
    const auto package = codec.parse_file(*options.manifest_path);
    if (!package) {
        errors << package.error().message;
        if (package.error().byte_offset.has_value()) {
            errors << " at byte " << *package.error().byte_offset;
        }
        errors << '\n';
        if (package.error().code == modding::ManifestErrorCode::source_error) {
            return static_cast<int>(ModValidatorExitCode::source_error);
        }
        return static_cast<int>(ModValidatorExitCode::invalid_manifest);
    }

    const auto& value = package.value();
    if (options.check_assets) {
        modding::LoadedPackageSet package_set;
        package_set.packages.push_back(value);
        package_set.manifest_paths.push_back(*options.manifest_path);

        const filesystem::NativeReadOnlyFilesystem filesystem;
        const modding::PackageAssetVerifier verifier(filesystem);
        const auto issues = verifier.verify(package_set);
        if (!issues.empty()) {
            errors << issues.front().message
                   << " (package "
                   << issues.front().package.value();
            if (issues.front().asset.valid()) {
                errors << ", asset " << issues.front().asset.value();
            }
            errors << ")\n";
            return static_cast<int>(ModValidatorExitCode::invalid_assets);
        }
    }

    if (options.json) {
        output << "{\"package_id\":\""
               << json_escape(value.id.value())
               << "\",\"version\":\""
               << value.version.major
               << '.'
               << value.version.minor
               << '.'
               << value.version.patch
               << "\",\"assets\":"
               << value.assets.size()
               << ",\"navigation_graphs\":"
               << value.navigation_graphs.size()
               << ",\"maps\":"
               << value.maps.size()
               << ",\"missions\":"
               << value.missions.size()
               << ",\"assets_checked\":"
               << (options.check_assets ? "true" : "false")
               << "}\n";
    } else {
        output << "Package: " << value.id.value() << '\n'
               << "Version: "
               << value.version.major
               << '.'
               << value.version.minor
               << '.'
               << value.version.patch
               << '\n'
               << "Assets: " << value.assets.size() << '\n'
               << "Navigation graphs: " << value.navigation_graphs.size() << '\n'
               << "Maps: " << value.maps.size() << '\n'
               << "Missions: " << value.missions.size() << '\n'
               << "Assets checked: "
               << (options.check_assets ? "yes" : "no")
               << '\n';
    }
    return static_cast<int>(ModValidatorExitCode::success);
}

void print_mod_validator_help(std::ostream& output) {
    output
        << "Usage: contract-mod-validate --manifest <file>"
        << " [--check-assets] [--json] [--help]\n"
        << "Validates an original OpenContract mod manifest without extracting data.\n";
}

}

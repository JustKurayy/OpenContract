#include <contract/runtime/Runtime.hpp>

#include <contract/modding/PackageSet.hpp>
#include <contract/runtime/RuntimeSession.hpp>
#include <contract/runtime/RuntimeWorld.hpp>

#include <chrono>
#include <cstddef>
#include <ostream>

namespace contract::runtime {
namespace {

constexpr auto initial_simulation_step =
    std::chrono::nanoseconds{16'666'667};
constexpr std::size_t initial_catch_up_limit = 4;
constexpr std::size_t initial_pending_command_limit = 1024;

}

std::optional<RuntimeOptions> parse_runtime_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors) {
    RuntimeOptions options;
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
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            errors << "Unknown option: " << argument << '\n';
            return std::nullopt;
        }
    }
    return options;
}

void print_runtime_help(std::ostream& output) {
    output
        << "Usage: contract-runtime [--game-path <directory>]"
        << " [--mod-manifest <file>]... [--mission <identifier>] [--help]\n";
}

int run_runtime(
    const RuntimeOptions& options,
    const RuntimeContext& context,
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
        context.diagnostics.emit(
            {
                diagnostics::Severity::error,
                "runtime.path",
                "No candidate game-data directory was supplied or discovered",
                std::nullopt,
                std::nullopt
            });
        errors << "[runtime.path] No candidate game-data directory was supplied or discovered\n";
        return static_cast<int>(RuntimeExitCode::game_path_missing);
    }

    const auto report = validator.validate(*selected_path);
    if (report.status != installation::InstallationStatus::plausible_installation) {
        context.diagnostics.emit(
            {
                diagnostics::Severity::error,
                "runtime.validation",
                installation::status_name(report.status),
                report.canonical_path,
                std::nullopt
            });
        errors << "[runtime.validation] "
               << installation::status_name(report.status)
               << " (structural validation only)\n";
        return static_cast<int>(RuntimeExitCode::installation_invalid);
    }

    std::optional<modding::LoadedPackageSet> package_set;
    if (!options.mod_manifests.empty()) {
        const modding::PackageSetLoader loader;
        auto loaded = loader.load(options.mod_manifests);
        if (!loaded.has_value()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.mod-set",
                    loaded.error().message,
                    loaded.error().path.empty()
                        ? std::optional<std::filesystem::path>{}
                        : std::optional<std::filesystem::path>{
                              loaded.error().path},
                    std::nullopt
                });
            errors << "[runtime.mod-set] " << loaded.error().message;
            if (!loaded.error().path.empty()) {
                errors << ": " << loaded.error().path.string();
            }
            errors << '\n';
            return static_cast<int>(RuntimeExitCode::mod_set_invalid);
        }
        const modding::PackageAssetVerifier asset_verifier(context.filesystem);
        const auto asset_issues = asset_verifier.verify(loaded.value());
        if (!asset_issues.empty()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.mod-assets",
                    asset_issues.front().message,
                    std::nullopt,
                    std::nullopt
                });
            errors << "[runtime.mod-assets] "
                   << asset_issues.front().message
                   << " (package "
                   << asset_issues.front().package.value();
            if (asset_issues.front().asset.valid()) {
                errors << ", asset "
                       << asset_issues.front().asset.value();
            }
            errors << ")\n";
            return static_cast<int>(RuntimeExitCode::mod_set_invalid);
        }
        package_set = std::move(loaded.value());
    }

    std::optional<RuntimeSession> session;
    if (options.mission.has_value()) {
        if (!package_set.has_value()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.mission",
                    "A mission requires at least one mod manifest",
                    std::nullopt,
                    std::nullopt
                });
            errors << "[runtime.mission] A mission requires at least one mod manifest\n";
            return static_cast<int>(RuntimeExitCode::mission_invalid);
        }
        const RuntimeWorldBuilder builder;
        auto built = builder.build(*package_set, *options.mission);
        if (!built.has_value()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.mission",
                    built.error().message,
                    std::nullopt,
                    std::nullopt
                });
            errors << "[runtime.mission] " << built.error().message << '\n';
            return static_cast<int>(RuntimeExitCode::mission_invalid);
        }
        auto created_session = RuntimeSession::create(
            std::move(built.value()),
            initial_simulation_step,
            initial_catch_up_limit,
            initial_pending_command_limit);
        if (!created_session.has_value()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.session",
                    created_session.error().message,
                    std::nullopt,
                    std::nullopt
                });
            errors << "[runtime.session] "
                   << created_session.error().message
                   << '\n';
            return static_cast<int>(RuntimeExitCode::session_invalid);
        }
        session = std::move(created_session.value());
    }

    const auto package_count = package_set.has_value()
        ? package_set->packages.size()
        : std::size_t{0};
    output << "[runtime.not-implemented] Runtime not implemented; validated "
           << package_count
           << " mod package";
    if (package_count != 1) {
        output << 's';
    }
    if (options.mission.has_value()) {
        output << "; selected mission " << *options.mission;
        output << "; initialized "
               << session->world().entities().size()
               << " entities and "
               << session->world().objectives().size()
               << " objectives; simulation step "
               << initial_simulation_step.count()
               << " ns";
    }
    output << '\n';
    context.diagnostics.emit(
        {
            diagnostics::Severity::information,
            "runtime.not-implemented",
            "Runtime not implemented",
            report.canonical_path,
            std::nullopt
        });
    return static_cast<int>(RuntimeExitCode::not_implemented);
}

int run_runtime(
    const RuntimeOptions& options,
    std::ostream& output,
    std::ostream& errors) {
    const filesystem::NativeReadOnlyFilesystem filesystem;
    diagnostics::DiagnosticBuffer diagnostics;
    const RuntimeContext context{
        filesystem,
        diagnostics,
        installation::default_recognition_policy(),
        installation::environment_game_path(),
        installation::configured_game_path(),
        installation::default_windows_probe_paths()};
    return run_runtime(options, context, output, errors);
}

}

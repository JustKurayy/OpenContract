#include <contract/runtime/Runtime.hpp>

#include <contract/modding/PackageSet.hpp>
#include <contract/runtime/RuntimeSession.hpp>
#include <contract/runtime/RuntimeWorld.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <ostream>
#include <system_error>

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
        } else if (argument == "--source-mission") {
            if (index + 1 >= arguments.size()) {
                errors << "--source-mission requires an identifier\n";
                return std::nullopt;
            }
            options.source_mission = arguments[++index];
        } else if (argument == "--max-frames") {
            if (index + 1 >= arguments.size()) {
                errors << "--max-frames requires a positive integer\n";
                return std::nullopt;
            }
            const auto& value = arguments[++index];
            std::uint64_t parsed = 0;
            const auto result = std::from_chars(
                value.data(),
                value.data() + value.size(),
                parsed);
            if (result.ec != std::errc{} ||
                result.ptr != value.data() + value.size() ||
                parsed == 0) {
                errors << "--max-frames requires a positive integer\n";
                return std::nullopt;
            }
            options.maximum_frames = parsed;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            errors << "Unknown option: " << argument << '\n';
            return std::nullopt;
        }
    }
    if (options.mission.has_value() &&
        options.source_mission.has_value()) {
        errors << "--mission and --source-mission are mutually exclusive\n";
        return std::nullopt;
    }
    return options;
}

void print_runtime_help(std::ostream& output) {
    output
        << "Usage: contract-runtime [--game-path <directory>]"
        << " [--mod-manifest <file>]... [--mission <identifier>]"
        << " [--source-mission <identifier>]"
        << " [--max-frames <count>] [--help]\n";
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

    std::optional<mission::SourceMissionLoadResult> source_mission;
    if (options.source_mission.has_value()) {
        if (context.source_mission_loader == nullptr) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.source-mission",
                    "No source mission loader is configured",
                    report.canonical_path,
                    std::nullopt
                });
            errors
                << "[runtime.source-mission] No source mission loader is configured\n";
            return static_cast<int>(RuntimeExitCode::mission_invalid);
        }
        auto loaded = context.source_mission_loader->load(
            report.canonical_path.value_or(report.requested_path),
            *options.source_mission);
        if (!loaded.has_value()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.source-mission",
                    loaded.error().message,
                    loaded.error().path.empty()
                        ? report.canonical_path
                        : std::optional<std::filesystem::path>{
                              loaded.error().path},
                    std::nullopt
                });
            errors << "[runtime.source-mission] "
                   << loaded.error().message;
            if (!loaded.error().path.empty()) {
                errors << ": " << loaded.error().path.string();
            }
            errors << '\n';
            return static_cast<int>(RuntimeExitCode::mission_invalid);
        }
        source_mission = std::move(loaded.value());
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
    } else if (source_mission.has_value()) {
        RuntimeWorld world(
            mission::MissionId(source_mission->mission_id),
            scene::MapId("source." + source_mission->mission_id),
            std::nullopt,
            {},
            {});
        auto created_session = RuntimeSession::create(
            std::move(world),
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
    if (session.has_value() && context.runner != nullptr) {
        auto host_result = RuntimeHost::create(std::move(*session), {});
        if (!host_result.has_value()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.host",
                    host_result.error().message,
                    std::nullopt,
                    std::nullopt
                });
            errors << "[runtime.host] "
                   << host_result.error().message
                   << '\n';
            return static_cast<int>(RuntimeExitCode::runtime_failed);
        }
        auto host = std::move(host_result.value());
        const auto initial = host.observe();
        output << "[runtime.boot] Booting "
               << package_count
               << " mod package";
        if (package_count != 1) {
            output << 's';
        }
        output << "; selected mission "
               << initial.mission.value()
               << "; initialized "
               << initial.entities.size()
               << " entities and "
               << initial.objectives.size()
               << " objectives; simulation step "
               << initial.simulation_step.count()
               << " ns\n";
        if (source_mission.has_value()) {
            std::size_t fallback_indices = 0;
            std::size_t materialless_indices = 0;
            for (const auto& batch :
                 source_mission->render_scene.batches) {
                if (!batch.texture_index.has_value()) {
                    fallback_indices += batch.index_count;
                }
                if (batch.source_material_id == 0U) {
                    materialless_indices += batch.index_count;
                }
            }
            output << "[runtime.source-mission] Loaded "
                   << source_mission->render_scene.source_mesh_count
                   << " meshes, "
                   << source_mission->render_scene.vertices.size()
                   << " vertices, and "
                   << source_mission->render_scene.indices.size()
                   << " indices across "
                   << source_mission->active_placements
                   << " active placements ("
                   << source_mission->inactive_placements
                   << " inactive, "
                   << source_mission->inherited_inactive_placements
                   << " inherited inactive, "
                   << source_mission->invisible_placements
                   << " invisible, and "
                   << source_mission->missing_placements
                   << " unsupported placements), "
                   << source_mission->visibility_group_count
                   << " visibility groups, "
                   << source_mission->collision_meshes
                   << " collision meshes suppressed, "
                   << source_mission->overlay_meshes
                   << " overlay meshes suppressed, "
                   << source_mission->texture_count
                   << " diffuse textures, and "
                   << source_mission->render_batch_count
                   << " render batches ("
                   << fallback_indices
                   << " fallback indices, "
                   << materialless_indices
                   << " materialless) from "
                   << source_mission->archive_path.string()
                   << '\n';
        }

        const auto run_result = context.runner->run(
            host,
            {
                options.maximum_frames,
                source_mission.has_value()
                    ? &source_mission->render_scene
                    : nullptr
            });
        if (!run_result.has_value()) {
            context.diagnostics.emit(
                {
                    diagnostics::Severity::error,
                    "runtime.runner",
                    run_result.error().message,
                    std::nullopt,
                    std::nullopt
                });
            errors << "[runtime.runner] "
                   << run_result.error().message
                   << '\n';
            return static_cast<int>(RuntimeExitCode::runtime_failed);
        }

        const auto final = host.observe();
        output << "[runtime.boot] Runtime closed after "
               << final.completed_ticks
               << " tick";
        if (final.completed_ticks != 1) {
            output << 's';
        }
        output << '\n';
        context.diagnostics.emit(
            {
                diagnostics::Severity::information,
                "runtime.boot",
                "Runtime closed cleanly",
                report.canonical_path,
                std::nullopt
            });
        return static_cast<int>(RuntimeExitCode::success);
    }

    output << "[runtime.not-implemented] Runtime not implemented; validated "
           << package_count
           << " mod package";
    if (package_count != 1) {
        output << 's';
    }
    if (options.mission.has_value()) {
        const auto observation = session->observe();
        output << "; selected mission " << *options.mission;
        output << "; initialized "
               << observation.entities.size()
               << " entities and "
               << observation.objectives.size()
               << " objectives; simulation step "
               << observation.simulation_step.count()
               << " ns; observation tick "
               << observation.completed_ticks
               << ", next event sequence "
               << observation.next_event_sequence;
    } else if (options.source_mission.has_value()) {
        output << "; selected source mission "
               << *options.source_mission;
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
        installation::default_windows_probe_paths(),
        nullptr,
        nullptr};
    return run_runtime(options, context, output, errors);
}

}

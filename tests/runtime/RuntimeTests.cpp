#include "TestSupport.hpp"

#include <contract/runtime/Runtime.hpp>

#include <contract/core/Result.hpp>
#include <contract/diagnostics/DiagnosticSink.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/modding/ModManifest.hpp>
#include <contract/mission/SourceMissionLoader.hpp>
#include <contract/runtime/RuntimeRunner.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using contract::core::Result;
using contract::filesystem::DirectoryEntry;
using contract::filesystem::FilesystemError;
using contract::filesystem::FilesystemErrorCode;
using contract::filesystem::IReadOnlyFilesystem;
using contract::filesystem::ReadOnlyBinaryFile;

class SyntheticFilesystem final : public IReadOnlyFilesystem {
public:
    Result<std::filesystem::path, FilesystemError> canonicalize(
        const std::filesystem::path& path) const override {
        if (error.has_value()) {
            return Result<std::filesystem::path, FilesystemError>::failure(*error);
        }
        const auto normalized = path.lexically_normal();
        if (missing_paths.contains(normalized.generic_string())) {
            return Result<std::filesystem::path, FilesystemError>::failure(
                {FilesystemErrorCode::path_missing, "Synthetic path is missing"});
        }
        return Result<std::filesystem::path, FilesystemError>::success(normalized);
    }

    Result<std::vector<DirectoryEntry>, FilesystemError> enumerate_top_level(
        const std::filesystem::path&) const override {
        if (error.has_value()) {
            return Result<std::vector<DirectoryEntry>, FilesystemError>::failure(*error);
        }
        return Result<std::vector<DirectoryEntry>, FilesystemError>::success(entries);
    }

    Result<ReadOnlyBinaryFile, FilesystemError> read_binary_file(
        const std::filesystem::path&,
        std::size_t) const override {
        ++binary_read_calls;
        return Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::io_error, "Binary reads are forbidden in this test"});
    }

    std::optional<FilesystemError> error;
    std::unordered_set<std::string> missing_paths;
    std::vector<DirectoryEntry> entries;
    mutable int binary_read_calls{0};
};

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                "contract-synthetic-runtime-test") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class RecordingRunner final : public contract::runtime::IRuntimeRunner {
public:
    contract::core::Result<void, contract::runtime::RuntimeRunnerError> run(
        contract::runtime::RuntimeHost& host,
        const contract::runtime::RuntimeRunnerOptions& options) override {
        ++calls;
        maximum_frames = options.maximum_frames;
        controlled_entity = options.controlled_entity;
        collision_index_count =
            options.collision_scene == nullptr
                ? 0
                : options.collision_scene->indices.size();
        player_model_vertex_count =
            options.player_model == nullptr
                ? 0
                : options.player_model->vertices.size();
        render_vertex_count = options.render_scene == nullptr
            ? 0
            : options.render_scene->vertices.size();
        initial_observation = host.observe();
        if (fail) {
            return contract::core::Result<
                void,
                contract::runtime::RuntimeRunnerError>::failure(
                {
                    contract::runtime::RuntimeRunnerErrorCode::platform_failed,
                    "Synthetic runner failure"
                });
        }
        const auto frame = host.advance(
            initial_observation.simulation_step,
            {});
        if (!frame.has_value()) {
            return contract::core::Result<
                void,
                contract::runtime::RuntimeRunnerError>::failure(
                {
                    contract::runtime::RuntimeRunnerErrorCode::host_advance_failed,
                    frame.error().message
                });
        }
        final_observation = frame.value().observation;
        return contract::core::Result<
            void,
            contract::runtime::RuntimeRunnerError>::success();
    }

    std::size_t calls{0};
    bool fail{false};
    std::optional<std::uint64_t> maximum_frames;
    std::optional<contract::scene::EntityId> controlled_entity;
    std::size_t collision_index_count{0};
    std::size_t player_model_vertex_count{0};
    std::size_t render_vertex_count{0};
    contract::runtime::RuntimeObservation initial_observation;
    contract::runtime::RuntimeObservation final_observation;
};

class SyntheticSourceMissionLoader final
    : public contract::mission::ISourceMissionLoader {
public:
    contract::core::Result<
        contract::mission::SourceMissionLoadResult,
        contract::mission::SourceMissionLoadError>
    load(
        const std::filesystem::path& game_path,
        std::string_view mission_id) const override {
        ++calls;
        selected_game_path = game_path;
        selected_mission = mission_id;
        contract::scene::RenderScene scene;
        scene.vertices = {
            {0.0F, 0.0F, 0.0F},
            {1.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F}};
        scene.indices = {0, 1, 2};
        scene.source_mesh_count = 1;
        contract::mission::SourceMissionLoadResult result;
        result.mission_id = std::string(mission_id);
        result.archive_path = game_path / "synthetic.zip";
        result.render_scene = std::move(scene);
        result.primitive_records = 9;
        result.declared_scene_objects = 1;
        result.decoded_placements = 1;
        result.active_placements = 1;
        result.preferred_spawn = contract::scene::Transform{
            {100.0F, 200.0F, 300.0F}};
        result.collision_scene.vertices = {
            {0.0F, 0.0F, 0.0F},
            {100.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 100.0F}
        };
        result.collision_scene.indices = {0, 1, 2};
        contract::scene::RenderScene player_model;
        player_model.vertices = {
            {0.0F, 0.0F, 0.0F},
            {1.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F}
        };
        player_model.indices = {0, 1, 2};
        player_model.batches.push_back({0, 3});
        result.player_model = std::move(player_model);
        result.animation_path_count = 3;
        result.animation_database_count = 1;
        result.animation_clip_count = 3;
        result.character_animations =
            contract::mission::SourceCharacterAnimations{
                "anmcol:animationdatabase#synthetic",
                {
                    "idle",
                    "/idle.synthetic",
                    2,
                    10,
                    20,
                    4,
                    128,
                    {
                        contract::mission::
                            SourceCharacterAnimationChannel{
                                1,
                                16,
                                std::vector<std::byte>(
                                    3,
                                    std::byte{0x11})},
                        contract::mission::
                            SourceCharacterAnimationChannel{
                                8,
                                3,
                                std::vector<std::byte>(
                                    2,
                                    std::byte{0x22})}
                    }
                },
                {
                    "walk",
                    "/walk.synthetic",
                    0,
                    18,
                    25,
                    8,
                    256,
                    {
                        contract::mission::
                            SourceCharacterAnimationChannel{
                                1,
                                8,
                                std::vector<std::byte>(
                                    4,
                                    std::byte{0x33})},
                        contract::mission::
                            SourceCharacterAnimationChannel{
                                4,
                                1,
                                std::vector<std::byte>(
                                    1,
                                    std::byte{0x44})}
                    }
                },
                {
                    "sprint",
                    "/sprint.synthetic",
                    1,
                    15,
                    25,
                    8,
                    320,
                    {
                        contract::mission::
                            SourceCharacterAnimationChannel{
                                1,
                                8,
                                std::vector<std::byte>(
                                    6,
                                    std::byte{0x55})}
                    }
                }
            };
        return contract::core::Result<
            contract::mission::SourceMissionLoadResult,
            contract::mission::SourceMissionLoadError>::success(
            std::move(result));
    }

    mutable std::size_t calls{0};
    mutable std::filesystem::path selected_game_path;
    mutable std::string selected_mission;
};

contract::modding::ModPackage package_with_mission(std::string mission_id) {
    using namespace contract;
    const scene::MapId map_id("map.synthetic");
    return {
        modding::ModPackageId("package.synthetic"),
        {1, 0, 0},
        {"Synthetic", "Synthetic Author", "Synthetic only"},
        {},
        {},
        {},
        {
            scene::MapDefinition{map_id, {}, std::nullopt}
        },
        {
            mission::MissionDefinition{
                mission::MissionId(std::move(mission_id)),
                map_id,
                {}}
        }};
}

void write_manifest(
    const std::filesystem::path& path,
    const contract::modding::ModPackage& package) {
    const contract::modding::ModManifestCodec codec;
    const auto encoded = codec.serialize(package);
    if (encoded.has_value()) {
        std::ofstream output(path, std::ios::binary);
        output << encoded.value();
    }
}

}

int main() {
    std::ostringstream errors;
    const auto options = contract::runtime::parse_runtime_options(
        {
            "--game-path",
            "C:/Synthetic Game",
            "--mod-manifest",
            "C:/Synthetic Mods/base.json",
            "--mod-manifest",
            "C:/Synthetic Mods/addon.json",
            "--mission",
            "mission.synthetic",
            "--max-frames",
            "3"
        },
        errors);

    CONTRACT_EXPECT(options.has_value());
    CONTRACT_EXPECT(errors.str().empty());
    CONTRACT_EXPECT_EQ(
        options->game_path.value(),
        std::filesystem::path("C:/Synthetic Game"));
    CONTRACT_EXPECT_EQ(options->mod_manifests.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(
        options->mod_manifests[0],
        std::filesystem::path("C:/Synthetic Mods/base.json"));
    CONTRACT_EXPECT_EQ(
        options->mission.value(),
        std::string("mission.synthetic"));
    CONTRACT_EXPECT_EQ(
        options->maximum_frames.value(),
        std::uint64_t{3});

    std::ostringstream source_parse_errors;
    const auto source_options = contract::runtime::parse_runtime_options(
        {"--source-mission", "M00"},
        source_parse_errors);
    CONTRACT_EXPECT(source_options.has_value());
    CONTRACT_EXPECT_EQ(
        source_options->source_mission.value(),
        std::string("M00"));
    CONTRACT_EXPECT(source_parse_errors.str().empty());

    std::ostringstream conflicting_errors;
    const auto conflicting = contract::runtime::parse_runtime_options(
        {"--mission", "custom", "--source-mission", "M00"},
        conflicting_errors);
    CONTRACT_EXPECT(!conflicting.has_value());
    CONTRACT_EXPECT(!conflicting_errors.str().empty());

    std::ostringstream missing_value_errors;
    const auto missing_value = contract::runtime::parse_runtime_options(
        {"--mod-manifest"},
        missing_value_errors);
    CONTRACT_EXPECT(!missing_value.has_value());
    CONTRACT_EXPECT(!missing_value_errors.str().empty());

    std::ostringstream unknown_errors;
    const auto unknown = contract::runtime::parse_runtime_options(
        {"--unknown"},
        unknown_errors);
    CONTRACT_EXPECT(!unknown.has_value());
    CONTRACT_EXPECT(!unknown_errors.str().empty());

    std::ostringstream invalid_frame_errors;
    const auto invalid_frames = contract::runtime::parse_runtime_options(
        {"--max-frames", "0"},
        invalid_frame_errors);
    CONTRACT_EXPECT(!invalid_frames.has_value());
    CONTRACT_EXPECT(!invalid_frame_errors.str().empty());

    using contract::filesystem::EntryType;
    using contract::installation::RecognitionPolicy;
    using contract::runtime::RuntimeContext;
    using contract::runtime::RuntimeExitCode;
    using contract::runtime::RuntimeOptions;

    SyntheticFilesystem filesystem;
    filesystem.entries = {
        {{}, "engine.bin", EntryType::file, 64},
        {{}, "levels", EntryType::directory, 0}};
    contract::diagnostics::DiagnosticBuffer diagnostics;
    RecordingRunner runner;
    SyntheticSourceMissionLoader source_loader;
    const RuntimeContext context{
        filesystem,
        diagnostics,
        RecognitionPolicy{{"engine.bin"}, {"levels"}, 2},
        std::nullopt,
        std::nullopt,
        {},
        &runner,
        &source_loader};

    TemporaryDirectory temporary;
    const auto manifest_path = temporary.path() / "package.contract.json";
    write_manifest(manifest_path, package_with_mission("mission.synthetic"));

    RuntimeOptions runnable;
    runnable.game_path = std::filesystem::path("C:/synthetic-install");
    runnable.mod_manifests.push_back(manifest_path);
    runnable.mission = "mission.synthetic";
    runnable.maximum_frames = std::uint64_t{3};

    std::ostringstream runtime_output;
    std::ostringstream runtime_errors;
    const auto runtime_exit = contract::runtime::run_runtime(
        runnable,
        context,
        runtime_output,
        runtime_errors);
    CONTRACT_EXPECT_EQ(
        runtime_exit,
        static_cast<int>(RuntimeExitCode::success));
    CONTRACT_EXPECT(runtime_errors.str().empty());
    CONTRACT_EXPECT(
        runtime_output.str().find("runtime.boot") != std::string::npos);
    CONTRACT_EXPECT(
        runtime_output.str().find("1 mod package") != std::string::npos);
    CONTRACT_EXPECT(
        runtime_output.str().find("mission.synthetic") != std::string::npos);
    CONTRACT_EXPECT(
        runtime_output.str().find("initialized 0 entities and 0 objectives") !=
        std::string::npos);
    CONTRACT_EXPECT(
        runtime_output.str().find("simulation step 16666667 ns") !=
        std::string::npos);
    CONTRACT_EXPECT(
        runtime_output.str().find(
            "closed after 1 tick") !=
        std::string::npos);
    CONTRACT_EXPECT_EQ(runner.calls, std::size_t{1});
    CONTRACT_EXPECT_EQ(
        runner.maximum_frames.value(),
        std::uint64_t{3});
    CONTRACT_EXPECT_EQ(
        runner.initial_observation.completed_ticks,
        std::uint64_t{0});
    CONTRACT_EXPECT_EQ(
        runner.final_observation.completed_ticks,
        std::uint64_t{1});

    RuntimeOptions source_runnable;
    source_runnable.game_path =
        std::filesystem::path("C:/synthetic-install");
    source_runnable.source_mission = "M00";
    source_runnable.maximum_frames = std::uint64_t{1};
    std::ostringstream source_output;
    std::ostringstream source_errors;
    const auto source_exit = contract::runtime::run_runtime(
        source_runnable,
        context,
        source_output,
        source_errors);
    CONTRACT_EXPECT_EQ(
        source_exit,
        static_cast<int>(RuntimeExitCode::success));
    CONTRACT_EXPECT(source_errors.str().empty());
    CONTRACT_EXPECT_EQ(source_loader.calls, std::size_t{1});
    CONTRACT_EXPECT_EQ(source_loader.selected_mission, std::string("M00"));
    CONTRACT_EXPECT_EQ(runner.render_vertex_count, std::size_t{3});
    CONTRACT_EXPECT_EQ(
        runner.collision_index_count,
        std::size_t{3});
    CONTRACT_EXPECT_EQ(
        runner.player_model_vertex_count,
        std::size_t{3});
    CONTRACT_EXPECT(runner.controlled_entity.has_value());
    CONTRACT_EXPECT_EQ(
        runner.controlled_entity->value(),
        std::string("player.local"));
    CONTRACT_EXPECT_EQ(
        runner.initial_observation.entities.size(),
        std::size_t{1});
    CONTRACT_EXPECT_EQ(
        runner.initial_observation.objectives.size(),
        std::size_t{1});
    CONTRACT_EXPECT_EQ(
        runner.initial_observation.entities[0].transform.position[0],
        100.0F);
    CONTRACT_EXPECT_EQ(
        runner.initial_observation.entities[0].transform.position[1],
        200.0F);
    CONTRACT_EXPECT_EQ(
        runner.initial_observation.entities[0].transform.position[2],
        300.0F);
    CONTRACT_EXPECT(
        source_output.str().find("Loaded 1 meshes") != std::string::npos);
    CONTRACT_EXPECT(
        source_output.str().find(
            "0 inactive, 0 inherited inactive, 0 invisible, and "
            "0 unsupported placements") !=
        std::string::npos);
    CONTRACT_EXPECT(
        source_output.str().find(
            "animation catalog with 3 paths, 1 database, and 3 clips") !=
        std::string::npos);
    CONTRACT_EXPECT(
        source_output.str().find(
            "idle source clip 2: 10 samples at 20 Hz, 4 tracks and "
            "128 encoded bytes across 2 channel directories and "
            "19 routed tracks with 5 encoded value bytes") !=
        std::string::npos);
    CONTRACT_EXPECT(
        source_output.str().find(
            "walk source clip 0: 18 samples at 25 Hz, 8 tracks and "
            "256 encoded bytes across 2 channel directories and "
            "9 routed tracks with 5 encoded value bytes") !=
        std::string::npos);
    CONTRACT_EXPECT(
        source_output.str().find(
            "sprint source clip 1: 15 samples at 25 Hz, 8 tracks and "
            "320 encoded bytes across 1 channel directory and "
            "8 routed tracks with 6 encoded value bytes") !=
        std::string::npos);
    CONTRACT_EXPECT_EQ(filesystem.binary_read_calls, 0);
    CONTRACT_EXPECT(!diagnostics.diagnostics().empty());
    CONTRACT_EXPECT_EQ(
        diagnostics.diagnostics().back().code,
        std::string("runtime.boot"));

    runner.fail = true;
    std::ostringstream runner_output;
    std::ostringstream runner_errors;
    const auto runner_exit = contract::runtime::run_runtime(
        runnable,
        context,
        runner_output,
        runner_errors);
    CONTRACT_EXPECT_EQ(
        runner_exit,
        static_cast<int>(RuntimeExitCode::runtime_failed));
    CONTRACT_EXPECT(
        runner_errors.str().find("Synthetic runner failure") !=
        std::string::npos);
    runner.fail = false;

    runnable.mission = "mission.missing";
    std::ostringstream mission_output;
    std::ostringstream mission_errors;
    const auto mission_exit = contract::runtime::run_runtime(
        runnable,
        context,
        mission_output,
        mission_errors);
    CONTRACT_EXPECT_EQ(
        mission_exit,
        static_cast<int>(RuntimeExitCode::mission_invalid));
    CONTRACT_EXPECT(
        mission_errors.str().find("mission.missing") != std::string::npos);

    runnable.mod_manifests = {temporary.path() / "missing.contract.json"};
    runnable.mission.reset();
    std::ostringstream package_output;
    std::ostringstream package_errors;
    const auto package_exit = contract::runtime::run_runtime(
        runnable,
        context,
        package_output,
        package_errors);
    CONTRACT_EXPECT_EQ(
        package_exit,
        static_cast<int>(RuntimeExitCode::mod_set_invalid));
    CONTRACT_EXPECT(!package_errors.str().empty());

    auto asset_package = package_with_mission("mission.synthetic");
    asset_package.assets.push_back(
        {
            contract::assets::AssetId("asset.missing"),
            std::filesystem::path("assets/missing.bin")
        });
    const auto asset_manifest_path =
        temporary.path() / "assets.contract.json";
    write_manifest(asset_manifest_path, asset_package);
    filesystem.missing_paths.insert(
        (temporary.path() / "assets" / "missing.bin")
            .lexically_normal()
            .generic_string());
    runnable.mod_manifests = {asset_manifest_path};
    runnable.mission.reset();
    std::ostringstream asset_output;
    std::ostringstream asset_errors;
    const auto asset_exit = contract::runtime::run_runtime(
        runnable,
        context,
        asset_output,
        asset_errors);
    CONTRACT_EXPECT_EQ(
        asset_exit,
        static_cast<int>(RuntimeExitCode::mod_set_invalid));
    CONTRACT_EXPECT(
        asset_errors.str().find("asset.missing") != std::string::npos);
    filesystem.missing_paths.clear();

    RuntimeOptions no_path;
    std::ostringstream path_output;
    std::ostringstream path_errors;
    const auto path_exit = contract::runtime::run_runtime(
        no_path,
        context,
        path_output,
        path_errors);
    CONTRACT_EXPECT_EQ(
        path_exit,
        static_cast<int>(RuntimeExitCode::game_path_missing));
    CONTRACT_EXPECT(!path_errors.str().empty());

    return contract::test::finish();
}

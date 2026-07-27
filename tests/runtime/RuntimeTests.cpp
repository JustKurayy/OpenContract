#include "TestSupport.hpp"

#include <contract/runtime/Runtime.hpp>

#include <contract/core/Result.hpp>
#include <contract/diagnostics/DiagnosticSink.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/modding/ModManifest.hpp>

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
            "mission.synthetic"
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
    const RuntimeContext context{
        filesystem,
        diagnostics,
        RecognitionPolicy{{"engine.bin"}, {"levels"}, 2},
        std::nullopt,
        std::nullopt,
        {}};

    TemporaryDirectory temporary;
    const auto manifest_path = temporary.path() / "package.contract.json";
    write_manifest(manifest_path, package_with_mission("mission.synthetic"));

    RuntimeOptions runnable;
    runnable.game_path = std::filesystem::path("C:/synthetic-install");
    runnable.mod_manifests.push_back(manifest_path);
    runnable.mission = "mission.synthetic";

    std::ostringstream runtime_output;
    std::ostringstream runtime_errors;
    const auto runtime_exit = contract::runtime::run_runtime(
        runnable,
        context,
        runtime_output,
        runtime_errors);
    CONTRACT_EXPECT_EQ(
        runtime_exit,
        static_cast<int>(RuntimeExitCode::not_implemented));
    CONTRACT_EXPECT(runtime_errors.str().empty());
    CONTRACT_EXPECT(
        runtime_output.str().find("runtime.not-implemented") != std::string::npos);
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
            "observation tick 0, next event sequence 0") !=
        std::string::npos);
    CONTRACT_EXPECT_EQ(filesystem.binary_read_calls, 0);
    CONTRACT_EXPECT(!diagnostics.diagnostics().empty());
    CONTRACT_EXPECT_EQ(
        diagnostics.diagnostics().back().code,
        std::string("runtime.not-implemented"));

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

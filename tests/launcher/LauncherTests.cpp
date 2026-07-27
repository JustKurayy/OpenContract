#include "TestSupport.hpp"

#include <contract/launcher/Launcher.hpp>

#include <contract/core/Result.hpp>

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
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
        return Result<std::filesystem::path, FilesystemError>::success(
            path.lexically_normal());
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
    std::vector<DirectoryEntry> entries;
    mutable int binary_read_calls{0};
};

class RecordingRuntimeProcess final : public contract::launcher::IRuntimeProcess {
public:
    int invoke(
        const std::filesystem::path& game_path,
        const std::vector<std::filesystem::path>& mod_manifests,
        const std::optional<std::string>& mission) const override {
        ++calls;
        received_game_path = game_path;
        received_manifests = mod_manifests;
        received_mission = mission;
        return exit_code;
    }

    int exit_code{10};
    mutable int calls{0};
    mutable std::filesystem::path received_game_path;
    mutable std::vector<std::filesystem::path> received_manifests;
    mutable std::optional<std::string> received_mission;
};

}

int main() {
    using contract::filesystem::EntryType;
    using contract::installation::RecognitionPolicy;
    using contract::launcher::LauncherContext;
    using contract::launcher::LauncherExitCode;
    using contract::launcher::LauncherOptions;

    std::ostringstream parse_errors;
    const auto parsed = contract::launcher::parse_launcher_options(
        {
            "--game-path",
            "C:/Synthetic Game",
            "--mod-manifest",
            "C:/Synthetic Mods/base.json",
            "--mod-manifest",
            "C:/Synthetic Mods/addon.json",
            "--mission",
            "mission.synthetic",
            "--run"
        },
        parse_errors);
    CONTRACT_EXPECT(parsed.has_value());
    CONTRACT_EXPECT(parse_errors.str().empty());
    CONTRACT_EXPECT(parsed->run);
    CONTRACT_EXPECT_EQ(parsed->mod_manifests.size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(parsed->mission.value(), std::string("mission.synthetic"));

    std::ostringstream unknown_errors;
    const auto unknown = contract::launcher::parse_launcher_options(
        {"--unknown"},
        unknown_errors);
    CONTRACT_EXPECT(!unknown.has_value());
    CONTRACT_EXPECT(!unknown_errors.str().empty());

    SyntheticFilesystem filesystem;
    filesystem.entries = {
        {{}, "engine.bin", EntryType::file, 64},
        {{}, "levels", EntryType::directory, 0}};
    RecordingRuntimeProcess process;
    const LauncherContext context{
        filesystem,
        RecognitionPolicy{{"engine.bin"}, {"levels"}, 2},
        std::nullopt,
        std::nullopt,
        {},
        process};

    LauncherOptions options;
    options.game_path = std::filesystem::path("C:/synthetic-install");
    options.mod_manifests = {
        std::filesystem::path("C:/Synthetic Mods/base.json")};
    options.mission = "mission.synthetic";

    std::ostringstream validation_output;
    std::ostringstream validation_errors;
    const auto validation_exit = contract::launcher::run_launcher(
        options,
        context,
        validation_output,
        validation_errors);
    CONTRACT_EXPECT_EQ(
        validation_exit,
        static_cast<int>(LauncherExitCode::success));
    CONTRACT_EXPECT(validation_errors.str().empty());
    CONTRACT_EXPECT(
        validation_output.str().find("structural validation only") !=
        std::string::npos);
    CONTRACT_EXPECT_EQ(process.calls, 0);
    CONTRACT_EXPECT_EQ(filesystem.binary_read_calls, 0);

    options.run = true;
    std::ostringstream run_output;
    std::ostringstream run_errors;
    const auto run_exit = contract::launcher::run_launcher(
        options,
        context,
        run_output,
        run_errors);
    CONTRACT_EXPECT_EQ(run_exit, process.exit_code);
    CONTRACT_EXPECT_EQ(process.calls, 1);
    CONTRACT_EXPECT_EQ(
        process.received_game_path,
        std::filesystem::path("C:/synthetic-install"));
    CONTRACT_EXPECT_EQ(process.received_manifests, options.mod_manifests);
    CONTRACT_EXPECT_EQ(process.received_mission, options.mission);

    filesystem.error = FilesystemError{
        FilesystemErrorCode::path_missing,
        "Synthetic path is missing"};
    std::ostringstream invalid_output;
    std::ostringstream invalid_errors;
    const auto invalid_exit = contract::launcher::run_launcher(
        options,
        context,
        invalid_output,
        invalid_errors);
    CONTRACT_EXPECT_EQ(
        invalid_exit,
        static_cast<int>(LauncherExitCode::installation_invalid));
    CONTRACT_EXPECT_EQ(process.calls, 1);

    CONTRACT_EXPECT_EQ(
        contract::launcher::quote_windows_argument(L"C:\\Synthetic Mods\\base.json"),
        std::wstring(L"\"C:\\Synthetic Mods\\base.json\""));
    CONTRACT_EXPECT_EQ(
        contract::launcher::quote_windows_argument(L"value\"quoted"),
        std::wstring(L"\"value\\\"quoted\""));
    CONTRACT_EXPECT_EQ(
        contract::launcher::quote_windows_argument(L"C:\\path with space\\"),
        std::wstring(L"\"C:\\path with space\\\\\""));

    const auto command_line = contract::launcher::build_windows_command_line(
        std::filesystem::path(L"C:\\Program Files\\contract-runtime.exe"),
        {L"--game-path", L"C:\\Synthetic Game"});
    CONTRACT_EXPECT_EQ(
        command_line,
        std::wstring(
            L"\"C:\\Program Files\\contract-runtime.exe\" "
            L"\"--game-path\" \"C:\\Synthetic Game\""));

    return contract::test::finish();
}

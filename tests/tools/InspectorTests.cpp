#include "TestSupport.hpp"

#include <contract/tools/ArchiveInspector.hpp>
#include <contract/tools/Inspector.hpp>

#include <contract/core/Result.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>

#include <cstddef>
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

}

int main() {
    using contract::filesystem::EntryType;
    using contract::installation::RecognitionPolicy;
    using contract::tools::InspectorContext;
    using contract::tools::InspectorExitCode;
    using contract::tools::InspectorOptions;

    std::ostringstream parse_errors;
    const auto parsed = contract::tools::parse_inspector_options(
        {"--game-path", "C:/Synthetic Path", "--json"},
        parse_errors);
    CONTRACT_EXPECT(parsed.has_value());
    CONTRACT_EXPECT(parsed->json);
    CONTRACT_EXPECT_EQ(
        parsed->game_path.value(),
        std::filesystem::path("C:/Synthetic Path"));
    CONTRACT_EXPECT(parse_errors.str().empty());

    std::ostringstream missing_value_errors;
    const auto missing_value = contract::tools::parse_inspector_options(
        {"--game-path"},
        missing_value_errors);
    CONTRACT_EXPECT(!missing_value.has_value());
    CONTRACT_EXPECT(!missing_value_errors.str().empty());

    std::ostringstream archive_parse_errors;
    const auto archive_options =
        contract::tools::parse_archive_inspector_options(
            {
                "--archive",
                "C:/Synthetic Path/scene.zip",
                "--primitive-entry",
                "synthetic/scene.prm",
                "--json"
            },
            archive_parse_errors);
    CONTRACT_EXPECT(archive_options.has_value());
    CONTRACT_EXPECT(archive_options->json);
    CONTRACT_EXPECT_EQ(
        archive_options->archive_path.value(),
        std::filesystem::path("C:/Synthetic Path/scene.zip"));
    CONTRACT_EXPECT_EQ(
        archive_options->primitive_entry.value(),
        std::string("synthetic/scene.prm"));
    CONTRACT_EXPECT(archive_parse_errors.str().empty());

    std::ostringstream missing_archive_errors;
    const auto missing_archive =
        contract::tools::parse_archive_inspector_options(
            {"--json"},
            missing_archive_errors);
    CONTRACT_EXPECT(!missing_archive.has_value());
    CONTRACT_EXPECT(!missing_archive_errors.str().empty());

    SyntheticFilesystem filesystem;
    filesystem.entries = {
        {{}, "engine.bin", EntryType::file, 64},
        {{}, "levels", EntryType::directory, 0}};
    const RecognitionPolicy policy{{"engine.bin"}, {"levels"}, 2};
    const InspectorContext context{
        filesystem,
        policy,
        std::nullopt,
        std::nullopt,
        {}};
    const InspectorOptions options{
        std::filesystem::path("C:/synthetic-install"),
        true,
        false};

    std::ostringstream output;
    std::ostringstream errors;
    const auto exit_code = contract::tools::run_inspector(
        options,
        context,
        output,
        errors);
    CONTRACT_EXPECT_EQ(
        exit_code,
        static_cast<int>(InspectorExitCode::success));
    CONTRACT_EXPECT(errors.str().empty());
    CONTRACT_EXPECT(output.str().find("\"validation\":\"plausible_installation\"") !=
                    std::string::npos);
    CONTRACT_EXPECT(output.str().find("\"probe\":\"unsupported\"") !=
                    std::string::npos);
    CONTRACT_EXPECT_EQ(filesystem.binary_read_calls, 0);

    filesystem.error = FilesystemError{
        FilesystemErrorCode::path_missing,
        "Synthetic path is missing"};
    std::ostringstream missing_output;
    std::ostringstream missing_errors;
    const auto missing_exit = contract::tools::run_inspector(
        options,
        context,
        missing_output,
        missing_errors);
    CONTRACT_EXPECT_EQ(
        missing_exit,
        static_cast<int>(InspectorExitCode::path_missing));
    CONTRACT_EXPECT_EQ(filesystem.binary_read_calls, 0);

    return contract::test::finish();
}

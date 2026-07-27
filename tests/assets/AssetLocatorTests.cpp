#include "TestSupport.hpp"

#include <contract/assets/AssetLocator.hpp>

#include <contract/core/Result.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using contract::core::Result;
using contract::filesystem::DirectoryEntry;
using contract::filesystem::FilesystemError;
using contract::filesystem::FilesystemErrorCode;
using contract::filesystem::IReadOnlyFilesystem;
using contract::filesystem::ReadOnlyBinaryFile;

class MappingFilesystem final : public IReadOnlyFilesystem {
public:
    Result<std::filesystem::path, FilesystemError> canonicalize(
        const std::filesystem::path& path) const override {
        const auto normalized = path.lexically_normal().generic_string();
        const auto match = canonical_paths.find(normalized);
        if (match == canonical_paths.end()) {
            return Result<std::filesystem::path, FilesystemError>::failure(
                {FilesystemErrorCode::path_missing, "Synthetic path is missing"});
        }
        return Result<std::filesystem::path, FilesystemError>::success(
            match->second);
    }

    Result<std::vector<DirectoryEntry>, FilesystemError> enumerate_top_level(
        const std::filesystem::path&) const override {
        return Result<std::vector<DirectoryEntry>, FilesystemError>::failure(
            {FilesystemErrorCode::io_error, "Not used"});
    }

    Result<ReadOnlyBinaryFile, FilesystemError> read_binary_file(
        const std::filesystem::path&,
        std::size_t) const override {
        return Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::io_error, "Not used"});
    }

    std::unordered_map<std::string, std::filesystem::path> canonical_paths;
};

}

int main() {
    using namespace contract::assets;

    MappingFilesystem filesystem;
    filesystem.canonical_paths.emplace(
        "C:/packages/base",
        std::filesystem::path("C:/packages/base"));
    filesystem.canonical_paths.emplace(
        "C:/packages/base/assets/item.bin",
        std::filesystem::path("C:/packages/base/assets/item.bin"));
    filesystem.canonical_paths.emplace(
        "C:/packages/base/assets/link.bin",
        std::filesystem::path("C:/outside/item.bin"));

    const AssetLocator locator(filesystem);
    const auto resolved = locator.resolve(
        std::filesystem::path("C:/packages/base"),
        {AssetId("asset.synthetic"), std::filesystem::path("assets/item.bin")});
    CONTRACT_EXPECT(resolved.has_value());
    CONTRACT_EXPECT_EQ(
        resolved.value(),
        std::filesystem::path("C:/packages/base/assets/item.bin"));

    const auto traversal = locator.resolve(
        std::filesystem::path("C:/packages/base"),
        {AssetId("asset.traversal"), std::filesystem::path("../outside.bin")});
    CONTRACT_EXPECT(!traversal.has_value());
    CONTRACT_EXPECT_EQ(
        traversal.error().code,
        AssetLocationErrorCode::invalid_source);

    const auto escaped = locator.resolve(
        std::filesystem::path("C:/packages/base"),
        {AssetId("asset.link"), std::filesystem::path("assets/link.bin")});
    CONTRACT_EXPECT(!escaped.has_value());
    CONTRACT_EXPECT_EQ(
        escaped.error().code,
        AssetLocationErrorCode::source_outside_package);

    const auto missing_root = locator.resolve(
        std::filesystem::path("C:/packages/missing"),
        {AssetId("asset.synthetic"), std::filesystem::path("assets/item.bin")});
    CONTRACT_EXPECT(!missing_root.has_value());
    CONTRACT_EXPECT_EQ(
        missing_root.error().code,
        AssetLocationErrorCode::package_root_error);

    const auto missing_source = locator.resolve(
        std::filesystem::path("C:/packages/base"),
        {AssetId("asset.missing"), std::filesystem::path("assets/missing.bin")});
    CONTRACT_EXPECT(!missing_source.has_value());
    CONTRACT_EXPECT_EQ(
        missing_source.error().code,
        AssetLocationErrorCode::source_error);

    return contract::test::finish();
}

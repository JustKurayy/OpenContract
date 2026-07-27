#pragma once

#include <contract/core/Result.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace contract::filesystem {

enum class FilesystemErrorCode {
    path_missing,
    not_directory,
    permission_denied,
    not_a_file,
    size_limit_exceeded,
    overflow,
    io_error,
    invalid_view
};

struct FilesystemError {
    FilesystemErrorCode code{FilesystemErrorCode::io_error};
    std::string message;
};

enum class EntryType {
    file,
    directory,
    other
};

struct DirectoryEntry {
    std::filesystem::path path;
    std::string name;
    EntryType type{EntryType::other};
    std::uintmax_t size{0};
};

class ReadOnlyBinaryFile {
public:
    ReadOnlyBinaryFile() = default;
    explicit ReadOnlyBinaryFile(std::vector<std::byte> bytes);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] core::Result<std::span<const std::byte>, FilesystemError> view(
        std::size_t offset,
        std::size_t length) const;

private:
    std::vector<std::byte> bytes_;
};

class IReadOnlyFilesystem {
public:
    virtual ~IReadOnlyFilesystem() = default;

    [[nodiscard]] virtual core::Result<std::filesystem::path, FilesystemError> canonicalize(
        const std::filesystem::path& path) const = 0;

    [[nodiscard]] virtual core::Result<std::vector<DirectoryEntry>, FilesystemError>
    enumerate_top_level(const std::filesystem::path& path) const = 0;

    [[nodiscard]] virtual core::Result<ReadOnlyBinaryFile, FilesystemError> read_binary_file(
        const std::filesystem::path& path,
        std::size_t maximum_size) const = 0;
};

class NativeReadOnlyFilesystem final : public IReadOnlyFilesystem {
public:
    [[nodiscard]] core::Result<std::filesystem::path, FilesystemError> canonicalize(
        const std::filesystem::path& path) const override;

    [[nodiscard]] core::Result<std::vector<DirectoryEntry>, FilesystemError>
    enumerate_top_level(const std::filesystem::path& path) const override;

    [[nodiscard]] core::Result<ReadOnlyBinaryFile, FilesystemError> read_binary_file(
        const std::filesystem::path& path,
        std::size_t maximum_size) const override;
};

}

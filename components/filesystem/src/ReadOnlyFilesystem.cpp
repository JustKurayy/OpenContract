#include <contract/filesystem/ReadOnlyFilesystem.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace contract::filesystem {
namespace {

FilesystemError translate_error(
    const std::error_code& error,
    std::string message,
    FilesystemErrorCode fallback = FilesystemErrorCode::io_error) {
    if (error == std::errc::no_such_file_or_directory) {
        return {FilesystemErrorCode::path_missing, std::move(message)};
    }
    if (error == std::errc::permission_denied) {
        return {FilesystemErrorCode::permission_denied, std::move(message)};
    }
    if (error == std::errc::not_a_directory) {
        return {FilesystemErrorCode::not_directory, std::move(message)};
    }
    return {fallback, std::move(message)};
}

}

ReadOnlyBinaryFile::ReadOnlyBinaryFile(std::vector<std::byte> bytes)
    : bytes_(std::move(bytes)) {}

std::size_t ReadOnlyBinaryFile::size() const noexcept {
    return bytes_.size();
}

std::span<const std::byte> ReadOnlyBinaryFile::bytes() const noexcept {
    return bytes_;
}

core::Result<std::span<const std::byte>, FilesystemError> ReadOnlyBinaryFile::view(
    std::size_t offset,
    std::size_t length) const {
    if (offset > bytes_.size() || length > bytes_.size() - offset) {
        return core::Result<std::span<const std::byte>, FilesystemError>::failure(
            {FilesystemErrorCode::invalid_view, "Requested view exceeds the file bounds"});
    }
    return core::Result<std::span<const std::byte>, FilesystemError>::success(
        std::span<const std::byte>(bytes_).subspan(offset, length));
}

core::Result<std::filesystem::path, FilesystemError>
NativeReadOnlyFilesystem::canonicalize(const std::filesystem::path& path) const {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        return core::Result<std::filesystem::path, FilesystemError>::failure(
            translate_error(error, "Unable to inspect path"));
    }
    if (!std::filesystem::exists(status)) {
        return core::Result<std::filesystem::path, FilesystemError>::failure(
            {FilesystemErrorCode::path_missing, "Path does not exist"});
    }

    const auto canonical = std::filesystem::canonical(path, error);
    if (error) {
        return core::Result<std::filesystem::path, FilesystemError>::failure(
            translate_error(error, "Unable to canonicalize path"));
    }
    return core::Result<std::filesystem::path, FilesystemError>::success(canonical);
}

core::Result<std::vector<DirectoryEntry>, FilesystemError>
NativeReadOnlyFilesystem::enumerate_top_level(const std::filesystem::path& path) const {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        return core::Result<std::vector<DirectoryEntry>, FilesystemError>::failure(
            translate_error(error, "Unable to inspect directory"));
    }
    if (!std::filesystem::exists(status)) {
        return core::Result<std::vector<DirectoryEntry>, FilesystemError>::failure(
            {FilesystemErrorCode::path_missing, "Directory does not exist"});
    }
    if (!std::filesystem::is_directory(status)) {
        return core::Result<std::vector<DirectoryEntry>, FilesystemError>::failure(
            {FilesystemErrorCode::not_directory, "Path is not a directory"});
    }

    std::vector<DirectoryEntry> entries;
    std::filesystem::directory_iterator iterator(
        path,
        std::filesystem::directory_options::none,
        error);
    if (error) {
        return core::Result<std::vector<DirectoryEntry>, FilesystemError>::failure(
            translate_error(error, "Unable to enumerate directory"));
    }

    for (const auto& native_entry : iterator) {
        const auto entry_status = native_entry.symlink_status(error);
        if (error) {
            return core::Result<std::vector<DirectoryEntry>, FilesystemError>::failure(
                translate_error(error, "Unable to inspect directory entry"));
        }

        EntryType type = EntryType::other;
        std::uintmax_t size = 0;
        if (std::filesystem::is_regular_file(entry_status)) {
            type = EntryType::file;
            size = native_entry.file_size(error);
            if (error) {
                return core::Result<std::vector<DirectoryEntry>, FilesystemError>::failure(
                    translate_error(error, "Unable to read file size"));
            }
        } else if (std::filesystem::is_directory(entry_status)) {
            type = EntryType::directory;
        }

        entries.push_back(
            {native_entry.path(), native_entry.path().filename().string(), type, size});
    }

    std::ranges::sort(entries, [](const DirectoryEntry& left, const DirectoryEntry& right) {
        return left.name < right.name;
    });
    return core::Result<std::vector<DirectoryEntry>, FilesystemError>::success(
        std::move(entries));
}

core::Result<ReadOnlyBinaryFile, FilesystemError>
NativeReadOnlyFilesystem::read_binary_file(
    const std::filesystem::path& path,
    std::size_t maximum_size) const {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            translate_error(error, "Unable to inspect file"));
    }
    if (!std::filesystem::exists(status)) {
        return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::path_missing, "File does not exist"});
    }
    if (!std::filesystem::is_regular_file(status)) {
        return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::not_a_file, "Path is not a regular file"});
    }

    const auto native_size = std::filesystem::file_size(path, error);
    if (error) {
        return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            translate_error(error, "Unable to determine file size"));
    }
    if (native_size > maximum_size) {
        return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::size_limit_exceeded, "File exceeds the configured read limit"});
    }
    if (native_size > std::numeric_limits<std::size_t>::max() ||
        native_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::overflow, "File size cannot be represented safely"});
    }

    const auto size = static_cast<std::size_t>(native_size);
    std::vector<std::byte> bytes(size);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
            {FilesystemErrorCode::permission_denied, "Unable to open file for reading"});
    }

    if (size != 0) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
        if (!input || static_cast<std::size_t>(input.gcount()) != size) {
            return core::Result<ReadOnlyBinaryFile, FilesystemError>::failure(
                {FilesystemErrorCode::io_error, "Unable to read the complete file"});
        }
    }

    return core::Result<ReadOnlyBinaryFile, FilesystemError>::success(
        ReadOnlyBinaryFile(std::move(bytes)));
}

}

#include <contract/datasource/DataSource.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace contract::datasource {
namespace {

DataSourceError file_error(
    const std::error_code& error,
    std::uint64_t offset,
    std::string message) {
    if (error == std::errc::no_such_file_or_directory) {
        return {DataSourceErrorCode::path_missing, offset, std::move(message)};
    }
    if (error == std::errc::permission_denied) {
        return {DataSourceErrorCode::permission_denied, offset, std::move(message)};
    }
    return {DataSourceErrorCode::io_error, offset, std::move(message)};
}

core::Result<void, DataSourceError> validate_range(
    std::uint64_t source_size,
    std::uint64_t offset,
    std::size_t length) {
    const auto length64 = static_cast<std::uint64_t>(length);
    if (offset > std::numeric_limits<std::uint64_t>::max() - length64) {
        return core::Result<void, DataSourceError>::failure(
            {DataSourceErrorCode::offset_overflow,
             offset,
             "Read end offset would overflow"});
    }
    if (offset + length64 > source_size) {
        return core::Result<void, DataSourceError>::failure(
            {DataSourceErrorCode::end_of_source,
             offset,
             "Read exceeds the data source bounds"});
    }
    return core::Result<void, DataSourceError>::success();
}

}

ReadBudget::ReadBudget(std::uint64_t total_limit, std::size_t per_read_limit)
    : total_limit_(total_limit), per_read_limit_(per_read_limit) {}

std::uint64_t ReadBudget::total_limit() const noexcept {
    return total_limit_;
}

std::size_t ReadBudget::per_read_limit() const noexcept {
    return per_read_limit_;
}

std::uint64_t ReadBudget::consumed() const noexcept {
    return consumed_;
}

std::uint64_t ReadBudget::remaining() const noexcept {
    return total_limit_ - consumed_;
}

core::Result<void, DataSourceError> ReadBudget::consume(
    std::uint64_t offset,
    std::size_t length) {
    if (length > per_read_limit_) {
        return core::Result<void, DataSourceError>::failure(
            {DataSourceErrorCode::read_limit_exceeded,
             offset,
             "Read exceeds the per-read byte limit"});
    }

    const auto length64 = static_cast<std::uint64_t>(length);
    if (length64 > remaining()) {
        return core::Result<void, DataSourceError>::failure(
            {DataSourceErrorCode::total_budget_exceeded,
             offset,
             "Read exceeds the remaining total byte budget"});
    }

    consumed_ += length64;
    return core::Result<void, DataSourceError>::success();
}

MemoryDataSource::MemoryDataSource(std::span<const std::byte> bytes)
    : bytes_(bytes.begin(), bytes.end()) {}

std::uint64_t MemoryDataSource::size() const noexcept {
    return static_cast<std::uint64_t>(bytes_.size());
}

core::Result<std::vector<std::byte>, DataSourceError> MemoryDataSource::read(
    std::uint64_t offset,
    std::size_t length,
    ReadBudget& budget) const {
    const auto range = validate_range(size(), offset, length);
    if (!range) {
        return core::Result<std::vector<std::byte>, DataSourceError>::failure(
            range.error());
    }

    const auto authorized = budget.consume(offset, length);
    if (!authorized) {
        return core::Result<std::vector<std::byte>, DataSourceError>::failure(
            authorized.error());
    }

    const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
    return core::Result<std::vector<std::byte>, DataSourceError>::success(
        std::vector<std::byte>(begin, begin + static_cast<std::ptrdiff_t>(length)));
}

FileDataSource::FileDataSource(std::filesystem::path path, std::uint64_t size)
    : path_(std::move(path)), size_(size) {}

core::Result<FileDataSource, DataSourceError> FileDataSource::open(
    std::filesystem::path path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        return core::Result<FileDataSource, DataSourceError>::failure(
            file_error(error, 0, "Unable to inspect data source path"));
    }
    if (!std::filesystem::exists(status)) {
        return core::Result<FileDataSource, DataSourceError>::failure(
            {DataSourceErrorCode::path_missing, 0, "Data source path does not exist"});
    }
    if (!std::filesystem::is_regular_file(status)) {
        return core::Result<FileDataSource, DataSourceError>::failure(
            {DataSourceErrorCode::not_a_file, 0, "Data source path is not a regular file"});
    }

    const auto file_size = std::filesystem::file_size(path, error);
    if (error) {
        return core::Result<FileDataSource, DataSourceError>::failure(
            file_error(error, 0, "Unable to determine data source size"));
    }
    if (file_size > std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<FileDataSource, DataSourceError>::failure(
            {DataSourceErrorCode::offset_overflow,
             0,
             "Data source size cannot be represented"});
    }

    return core::Result<FileDataSource, DataSourceError>::success(
        FileDataSource(std::move(path), static_cast<std::uint64_t>(file_size)));
}

std::uint64_t FileDataSource::size() const noexcept {
    return size_;
}

core::Result<std::vector<std::byte>, DataSourceError> FileDataSource::read(
    std::uint64_t offset,
    std::size_t length,
    ReadBudget& budget) const {
    const auto range = validate_range(size_, offset, length);
    if (!range) {
        return core::Result<std::vector<std::byte>, DataSourceError>::failure(
            range.error());
    }
    if (offset > static_cast<std::uint64_t>(
                     std::numeric_limits<std::streamoff>::max()) ||
        length > static_cast<std::size_t>(
                     std::numeric_limits<std::streamsize>::max())) {
        return core::Result<std::vector<std::byte>, DataSourceError>::failure(
            {DataSourceErrorCode::offset_overflow,
             offset,
             "Read range cannot be represented by the native stream"});
    }

    const auto authorized = budget.consume(offset, length);
    if (!authorized) {
        return core::Result<std::vector<std::byte>, DataSourceError>::failure(
            authorized.error());
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        return core::Result<std::vector<std::byte>, DataSourceError>::failure(
            {DataSourceErrorCode::permission_denied,
             offset,
             "Unable to open data source for reading"});
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        return core::Result<std::vector<std::byte>, DataSourceError>::failure(
            {DataSourceErrorCode::io_error, offset, "Unable to seek data source"});
    }

    std::vector<std::byte> bytes(length);
    if (length != 0) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(length));
        if (!input || static_cast<std::size_t>(input.gcount()) != length) {
            return core::Result<std::vector<std::byte>, DataSourceError>::failure(
                {DataSourceErrorCode::io_error,
                 offset,
                 "Unable to read the complete requested range"});
        }
    }

    return core::Result<std::vector<std::byte>, DataSourceError>::success(
        std::move(bytes));
}

}

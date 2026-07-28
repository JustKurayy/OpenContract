#pragma once

#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace contract::formats {

enum class ZipCompressionMethod : std::uint16_t {
    stored = 0,
    deflate = 8
};

enum class ZipArchiveErrorCode {
    invalid_archive,
    unsupported_multi_disk,
    unsupported_zip64,
    unsupported_encryption,
    unsupported_compression,
    duplicate_entry,
    entry_not_found,
    size_limit_exceeded,
    corrupt_entry,
    source_error
};

struct ZipArchiveError {
    ZipArchiveErrorCode code{ZipArchiveErrorCode::invalid_archive};
    std::uint64_t offset{0};
    std::string message;
};

struct ZipArchiveLimits {
    std::size_t max_entries{4096};
    std::size_t max_name_length{1024};
    std::uint64_t max_central_directory_size{16U * 1024U * 1024U};
    std::uint64_t max_trailing_data_size{64U * 1024U};
};

struct ZipArchiveEntry {
    std::string name;
    ZipCompressionMethod compression{ZipCompressionMethod::stored};
    std::uint32_t crc32{0};
    std::uint64_t compressed_size{0};
    std::uint64_t uncompressed_size{0};
    std::uint64_t local_header_offset{0};
};

class ZipArchiveIndex {
public:
    [[nodiscard]] static core::Result<ZipArchiveIndex, ZipArchiveError> read(
        const datasource::IReadOnlyDataSource& source,
        datasource::ReadBudget& budget,
        ZipArchiveLimits limits = {});

    [[nodiscard]] const std::vector<ZipArchiveEntry>& entries() const noexcept;
    [[nodiscard]] const ZipArchiveEntry* find(std::string_view name) const noexcept;

    [[nodiscard]] core::Result<std::vector<std::byte>, ZipArchiveError> read_entry(
        const datasource::IReadOnlyDataSource& source,
        const ZipArchiveEntry& entry,
        datasource::ReadBudget& budget,
        std::uint64_t max_uncompressed_size) const;

private:
    explicit ZipArchiveIndex(std::vector<ZipArchiveEntry> entries);

    std::vector<ZipArchiveEntry> entries_;
};

}

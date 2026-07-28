#include <contract/formats/ZipArchive.hpp>

#include <contract/binaryio/SourceBinaryReader.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_set>
#include <utility>

namespace contract::formats {
namespace {

constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50U;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014b50U;
constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50U;
constexpr std::uint64_t kEndOfCentralDirectorySize = 22;
constexpr std::uint64_t kMaximumCommentSize = 65'535;
constexpr std::uint16_t kEncryptedFlag = 0x0001U;
constexpr std::uint16_t kZip64Sentinel16 = 0xffffU;
constexpr std::uint32_t kZip64Sentinel32 = 0xffffffffU;

ZipArchiveError source_error(
    std::uint64_t offset,
    const datasource::DataSourceError& error) {
    return {
        ZipArchiveErrorCode::source_error,
        offset,
        error.message
    };
}

ZipArchiveError reader_error(
    const binaryio::SourceBinaryError& error,
    std::string message) {
    return {
        ZipArchiveErrorCode::invalid_archive,
        error.offset,
        std::move(message)
    };
}

std::uint16_t little_u16(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
    const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(low | (high << 8U));
}

std::uint32_t little_u32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

core::Result<std::vector<std::byte>, ZipArchiveError> read_range(
    const datasource::IReadOnlyDataSource& source,
    std::uint64_t offset,
    std::uint64_t length,
    datasource::ReadBudget& budget) {
    if (length > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::size_limit_exceeded,
                offset,
                "Requested archive range cannot fit in memory"
            });
    }
    if (offset > source.size() || length > source.size() - offset) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::invalid_archive,
                offset,
                "Archive range exceeds the source bounds"
            });
    }

    std::vector<std::byte> result(static_cast<std::size_t>(length));
    std::uint64_t copied = 0;
    while (copied < length) {
        const auto remaining = length - copied;
        const auto chunk_size = static_cast<std::size_t>(std::min<std::uint64_t>(
            remaining,
            static_cast<std::uint64_t>(budget.per_read_limit())));
        if (chunk_size == 0) {
            return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::source_error,
                    offset + copied,
                    "Read budget does not permit archive reads"
                });
        }
        auto chunk = source.read(offset + copied, chunk_size, budget);
        if (!chunk.has_value()) {
            return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
                source_error(offset + copied, chunk.error()));
        }
        if (chunk.value().size() != chunk_size) {
            return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::source_error,
                    offset + copied,
                    "Data source returned an unexpected byte count"
                });
        }
        std::copy(
            chunk.value().begin(),
            chunk.value().end(),
            result.begin() + static_cast<std::ptrdiff_t>(copied));
        copied += static_cast<std::uint64_t>(chunk_size);
    }
    return core::Result<std::vector<std::byte>, ZipArchiveError>::success(
        std::move(result));
}

core::Result<std::uint64_t, ZipArchiveError> find_end_record(
    const datasource::IReadOnlyDataSource& source,
    datasource::ReadBudget& budget,
    const ZipArchiveLimits& limits) {
    if (source.size() < kEndOfCentralDirectorySize) {
        return core::Result<std::uint64_t, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::invalid_archive,
                0,
                "Source is too small to contain a ZIP end record"
            });
    }

    if (limits.max_trailing_data_size >
        std::numeric_limits<std::uint64_t>::max() -
            kEndOfCentralDirectorySize - kMaximumCommentSize) {
        return core::Result<std::uint64_t, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::size_limit_exceeded,
                0,
                "ZIP trailing-data search limit is too large"
            });
    }
    const auto search_size = std::min(
        source.size(),
        kEndOfCentralDirectorySize + kMaximumCommentSize +
            limits.max_trailing_data_size);
    const auto search_offset = source.size() - search_size;
    auto tail = read_range(source, search_offset, search_size, budget);
    if (!tail.has_value()) {
        return core::Result<std::uint64_t, ZipArchiveError>::failure(
            tail.error());
    }

    for (std::size_t cursor =
             tail.value().size() - static_cast<std::size_t>(kEndOfCentralDirectorySize) + 1U;
         cursor-- > 0;) {
        if (little_u32(tail.value(), cursor) != kEndOfCentralDirectorySignature) {
            continue;
        }
        const auto comment_length = little_u16(tail.value(), cursor + 20U);
        const auto record_end =
            cursor + static_cast<std::size_t>(kEndOfCentralDirectorySize) +
            comment_length;
        if (record_end <= tail.value().size() &&
            tail.value().size() - record_end <=
                limits.max_trailing_data_size) {
            return core::Result<std::uint64_t, ZipArchiveError>::success(
                search_offset + cursor);
        }
    }

    return core::Result<std::uint64_t, ZipArchiveError>::failure(
        {
            ZipArchiveErrorCode::invalid_archive,
            search_offset,
            "ZIP end record was not found"
        });
}

core::Result<void, ZipArchiveError> ensure_read(
    const core::Result<void, binaryio::SourceBinaryError>& result,
    std::string message) {
    if (result.has_value()) {
        return core::Result<void, ZipArchiveError>::success();
    }
    return core::Result<void, ZipArchiveError>::failure(
        reader_error(result.error(), std::move(message)));
}

}

ZipArchiveIndex::ZipArchiveIndex(std::vector<ZipArchiveEntry> entries)
    : entries_(std::move(entries)) {}

core::Result<ZipArchiveIndex, ZipArchiveError> ZipArchiveIndex::read(
    const datasource::IReadOnlyDataSource& source,
    datasource::ReadBudget& budget,
    ZipArchiveLimits limits) {
    const auto end_offset = find_end_record(source, budget, limits);
    if (!end_offset.has_value()) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            end_offset.error());
    }

    binaryio::SourceBinaryReader reader(source, budget);
    const auto seek_end = ensure_read(
        reader.seek(end_offset.value() + 4U),
        "ZIP end record is truncated");
    if (!seek_end.has_value()) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            seek_end.error());
    }

    const auto disk_number = reader.read_u16();
    const auto central_disk = reader.read_u16();
    const auto entries_on_disk = reader.read_u16();
    const auto total_entries = reader.read_u16();
    const auto central_size = reader.read_u32();
    const auto central_offset = reader.read_u32();
    const auto comment_length = reader.read_u16();
    if (!disk_number || !central_disk || !entries_on_disk || !total_entries ||
        !central_size || !central_offset || !comment_length) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::invalid_archive,
                end_offset.value(),
                "ZIP end record is truncated"
            });
    }

    if (total_entries.value() == kZip64Sentinel16 ||
        entries_on_disk.value() == kZip64Sentinel16 ||
        central_size.value() == kZip64Sentinel32 ||
        central_offset.value() == kZip64Sentinel32) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::unsupported_zip64,
                end_offset.value(),
                "ZIP64 archives are not supported"
            });
    }
    if (disk_number.value() != 0 || central_disk.value() != 0 ||
        entries_on_disk.value() != total_entries.value()) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::unsupported_multi_disk,
                end_offset.value(),
                "Multi-disk ZIP archives are not supported"
            });
    }
    if (total_entries.value() > limits.max_entries ||
        central_size.value() > limits.max_central_directory_size) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::size_limit_exceeded,
                end_offset.value(),
                "ZIP central directory exceeds configured limits"
            });
    }
    if (central_offset.value() > end_offset.value() ||
        central_size.value() > end_offset.value() - central_offset.value()) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::invalid_archive,
                central_offset.value(),
                "ZIP central directory exceeds the source bounds"
            });
    }

    const auto seek_central = ensure_read(
        reader.seek(central_offset.value()),
        "ZIP central directory offset is invalid");
    if (!seek_central.has_value()) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            seek_central.error());
    }

    std::vector<ZipArchiveEntry> entries;
    entries.reserve(total_entries.value());
    std::unordered_set<std::string> names;
    for (std::uint16_t index = 0; index < total_entries.value(); ++index) {
        const auto descriptor_offset = reader.offset();
        const auto signature = reader.read_u32();
        if (!signature || signature.value() != kCentralDirectorySignature) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::invalid_archive,
                    descriptor_offset,
                    "ZIP central directory entry has an invalid signature"
                });
        }

        const auto skip_versions = reader.read_bytes(4);
        const auto flags = reader.read_u16();
        const auto method = reader.read_u16();
        const auto skip_time_date = reader.read_bytes(4);
        const auto crc = reader.read_u32();
        const auto compressed_size = reader.read_u32();
        const auto uncompressed_size = reader.read_u32();
        const auto name_length = reader.read_u16();
        const auto extra_length = reader.read_u16();
        const auto entry_comment_length = reader.read_u16();
        const auto skip_disk_attributes = reader.read_bytes(8);
        const auto local_offset = reader.read_u32();
        if (!skip_versions || !flags || !method || !skip_time_date ||
            !crc || !compressed_size || !uncompressed_size || !name_length ||
            !extra_length || !entry_comment_length || !skip_disk_attributes ||
            !local_offset) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::invalid_archive,
                    descriptor_offset,
                    "ZIP central directory entry is truncated"
                });
        }

        if ((flags.value() & kEncryptedFlag) != 0) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::unsupported_encryption,
                    descriptor_offset,
                    "Encrypted ZIP entries are not supported"
                });
        }
        if (method.value() != static_cast<std::uint16_t>(
                                  ZipCompressionMethod::stored) &&
            method.value() != static_cast<std::uint16_t>(
                                  ZipCompressionMethod::deflate)) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::unsupported_compression,
                    descriptor_offset,
                    "ZIP entry uses an unsupported compression method"
                });
        }
        if (compressed_size.value() == kZip64Sentinel32 ||
            uncompressed_size.value() == kZip64Sentinel32 ||
            local_offset.value() == kZip64Sentinel32) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::unsupported_zip64,
                    descriptor_offset,
                    "ZIP64 entries are not supported"
                });
        }
        if (name_length.value() == 0 ||
            name_length.value() > limits.max_name_length) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::size_limit_exceeded,
                    descriptor_offset,
                    "ZIP entry name exceeds configured limits"
                });
        }

        auto name_bytes = read_range(
            source,
            reader.offset(),
            name_length.value(),
            budget);
        if (!name_bytes.has_value()) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                name_bytes.error());
        }
        const auto skip_name = ensure_read(
            reader.seek(reader.offset() + name_length.value()),
            "ZIP entry name is truncated");
        if (!skip_name.has_value()) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                skip_name.error());
        }
        std::string name;
        name.reserve(name_bytes.value().size());
        for (const auto byte : name_bytes.value()) {
            name.push_back(static_cast<char>(
                std::to_integer<unsigned char>(byte)));
        }
        if (!names.insert(name).second) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::duplicate_entry,
                    descriptor_offset,
                    "ZIP archive contains duplicate entry names"
                });
        }

        const auto skip_variable = ensure_read(
            reader.seek(
                reader.offset() + extra_length.value() +
                entry_comment_length.value()),
            "ZIP central directory variable data is truncated");
        if (!skip_variable.has_value()) {
            return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
                skip_variable.error());
        }

        entries.push_back(
            {
                std::move(name),
                static_cast<ZipCompressionMethod>(method.value()),
                crc.value(),
                compressed_size.value(),
                uncompressed_size.value(),
                local_offset.value()
            });
    }

    if (reader.offset() !=
        static_cast<std::uint64_t>(central_offset.value()) +
            central_size.value()) {
        return core::Result<ZipArchiveIndex, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::invalid_archive,
                reader.offset(),
                "ZIP central directory size does not match its entries"
            });
    }

    return core::Result<ZipArchiveIndex, ZipArchiveError>::success(
        ZipArchiveIndex(std::move(entries)));
}

const std::vector<ZipArchiveEntry>& ZipArchiveIndex::entries() const noexcept {
    return entries_;
}

const ZipArchiveEntry* ZipArchiveIndex::find(std::string_view name) const noexcept {
    const auto found = std::find_if(
        entries_.begin(),
        entries_.end(),
        [name](const ZipArchiveEntry& entry) {
            return entry.name == name;
        });
    return found == entries_.end() ? nullptr : &*found;
}

core::Result<std::vector<std::byte>, ZipArchiveError>
ZipArchiveIndex::read_entry(
    const datasource::IReadOnlyDataSource& source,
    const ZipArchiveEntry& entry,
    datasource::ReadBudget& budget,
    std::uint64_t max_uncompressed_size) const {
    if (entry.uncompressed_size > max_uncompressed_size ||
        entry.uncompressed_size >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        entry.compressed_size >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        entry.compressed_size > std::numeric_limits<uInt>::max() ||
        entry.uncompressed_size > std::numeric_limits<uInt>::max()) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::size_limit_exceeded,
                entry.local_header_offset,
                "ZIP entry exceeds configured or implementation limits"
            });
    }

    binaryio::SourceBinaryReader reader(source, budget);
    const auto seek_local = ensure_read(
        reader.seek(entry.local_header_offset),
        "ZIP local header offset is invalid");
    if (!seek_local.has_value()) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            seek_local.error());
    }
    const auto signature = reader.read_u32();
    const auto version = reader.read_u16();
    const auto flags = reader.read_u16();
    const auto method = reader.read_u16();
    const auto time_date = reader.read_bytes(4);
    const auto crc = reader.read_u32();
    const auto compressed_size = reader.read_u32();
    const auto uncompressed_size = reader.read_u32();
    const auto name_length = reader.read_u16();
    const auto extra_length = reader.read_u16();
    if (!signature || !version || !flags || !method || !time_date || !crc ||
        !compressed_size || !uncompressed_size || !name_length ||
        !extra_length) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::invalid_archive,
                entry.local_header_offset,
                "ZIP local entry header is truncated"
            });
    }
    if (signature.value() != kLocalHeaderSignature ||
        method.value() != static_cast<std::uint16_t>(entry.compression) ||
        (flags.value() & kEncryptedFlag) != 0) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::corrupt_entry,
                entry.local_header_offset,
                "ZIP local entry header disagrees with the central directory"
            });
    }

    auto local_name_bytes = read_range(
        source,
        reader.offset(),
        name_length.value(),
        budget);
    if (!local_name_bytes.has_value()) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            local_name_bytes.error());
    }
    const auto skip_name = ensure_read(
        reader.seek(reader.offset() + name_length.value()),
        "ZIP local entry name is truncated");
    if (!skip_name.has_value()) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            skip_name.error());
    }
    std::string local_name;
    local_name.reserve(local_name_bytes.value().size());
    for (const auto byte : local_name_bytes.value()) {
        local_name.push_back(static_cast<char>(
            std::to_integer<unsigned char>(byte)));
    }
    if (local_name != entry.name) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::corrupt_entry,
                entry.local_header_offset,
                "ZIP local entry name disagrees with the central directory"
            });
    }

    const auto data_offset = reader.offset() + extra_length.value();
    auto compressed = read_range(
        source,
        data_offset,
        entry.compressed_size,
        budget);
    if (!compressed.has_value()) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            compressed.error());
    }

    std::vector<std::byte> output;
    if (entry.compression == ZipCompressionMethod::stored) {
        if (entry.compressed_size != entry.uncompressed_size) {
            return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::corrupt_entry,
                    data_offset,
                    "Stored ZIP entry has mismatched sizes"
                });
        }
        output = std::move(compressed.value());
    } else {
        output.resize(static_cast<std::size_t>(entry.uncompressed_size));
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef*>(compressed.value().data());
        stream.avail_in = static_cast<uInt>(compressed.value().size());
        stream.next_out = reinterpret_cast<Bytef*>(output.data());
        stream.avail_out = static_cast<uInt>(output.size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::corrupt_entry,
                    data_offset,
                    "Could not initialize ZIP deflate decoder"
                });
        }
        const auto inflate_result = inflate(&stream, Z_FINISH);
        const auto end_result = inflateEnd(&stream);
        if (inflate_result != Z_STREAM_END || end_result != Z_OK ||
            stream.total_out != entry.uncompressed_size) {
            return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::corrupt_entry,
                    data_offset,
                    "ZIP deflate mismatch: result=" +
                        std::to_string(inflate_result) +
                        ", end=" + std::to_string(end_result) +
                        ", input=" + std::to_string(stream.total_in) +
                        "/" + std::to_string(entry.compressed_size) +
                        ", output=" + std::to_string(stream.total_out) +
                        "/" + std::to_string(entry.uncompressed_size)
                });
        }
        const auto trailing_size =
            entry.compressed_size - stream.total_in;
        if (trailing_size == 4) {
            const auto trailer_offset =
                static_cast<std::size_t>(stream.total_in);
            std::uint32_t expected_adler = 0;
            for (std::size_t index = 0; index < 4; ++index) {
                expected_adler =
                    (expected_adler << 8U) |
                    std::to_integer<std::uint32_t>(
                        compressed.value()[trailer_offset + index]);
            }
            const auto initial_adler = adler32(0, Z_NULL, 0);
            const auto actual_adler = static_cast<std::uint32_t>(
                adler32(
                    initial_adler,
                    reinterpret_cast<const Bytef*>(output.data()),
                    static_cast<uInt>(output.size())));
            if (expected_adler != actual_adler) {
                return core::Result<
                    std::vector<std::byte>,
                    ZipArchiveError>::failure(
                    {
                        ZipArchiveErrorCode::corrupt_entry,
                        data_offset + stream.total_in,
                        "ZIP deflate Adler-32 trailer does not match"
                    });
            }
        } else if (trailing_size != 0) {
            return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
                {
                    ZipArchiveErrorCode::corrupt_entry,
                    data_offset + stream.total_in,
                    "ZIP deflate stream has unsupported trailing data"
                });
        }
    }

    const auto actual_crc = static_cast<std::uint32_t>(
        crc32(
            0,
            reinterpret_cast<const Bytef*>(output.data()),
            static_cast<uInt>(output.size())));
    if (actual_crc != entry.crc32) {
        return core::Result<std::vector<std::byte>, ZipArchiveError>::failure(
            {
                ZipArchiveErrorCode::corrupt_entry,
                data_offset,
                "ZIP entry checksum does not match"
            });
    }
    return core::Result<std::vector<std::byte>, ZipArchiveError>::success(
        std::move(output));
}

}

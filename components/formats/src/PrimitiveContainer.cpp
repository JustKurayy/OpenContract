#include <contract/formats/PrimitiveContainer.hpp>

#include <contract/binaryio/SourceBinaryReader.hpp>

#include <limits>
#include <utility>

namespace contract::formats {
namespace {

constexpr std::uint64_t kHeaderSize = 16;
constexpr std::uint64_t kRecordDescriptorSize = 16;

PrimitiveContainerError reader_error(
    const binaryio::SourceBinaryError& error,
    std::string message) {
    auto code = PrimitiveContainerErrorCode::source_error;
    if (error.code == binaryio::SourceBinaryErrorCode::end_of_source) {
        code = PrimitiveContainerErrorCode::truncated;
    } else if (error.code == binaryio::SourceBinaryErrorCode::overflow) {
        code = PrimitiveContainerErrorCode::offset_overflow;
    }
    return {code, error.offset, std::move(message)};
}

}

PrimitiveContainerIndex::PrimitiveContainerIndex(
    PrimitiveContainerHeader header,
    std::vector<PrimitiveRecord> records)
    : header_(header),
      records_(std::move(records)) {}

core::Result<PrimitiveContainerIndex, PrimitiveContainerError>
PrimitiveContainerIndex::read(
    const datasource::IReadOnlyDataSource& source,
    datasource::ReadBudget& budget,
    PrimitiveContainerLimits limits) {
    if (source.size() < kHeaderSize) {
        return core::Result<
            PrimitiveContainerIndex,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::truncated,
                0,
                "Primitive container header is truncated"
            });
    }

    binaryio::SourceBinaryReader reader(source, budget);
    const auto directory_offset = reader.read_u32();
    const auto record_count = reader.read_u32();
    const auto repeated_directory_offset = reader.read_u32();
    const auto reserved = reader.read_u32();
    if (!directory_offset || !record_count || !repeated_directory_offset ||
        !reserved) {
        return core::Result<
            PrimitiveContainerIndex,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::truncated,
                0,
                "Primitive container header is truncated"
            });
    }
    if (directory_offset.value() != repeated_directory_offset.value() ||
        directory_offset.value() < kHeaderSize ||
        directory_offset.value() > source.size()) {
        return core::Result<
            PrimitiveContainerIndex,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::invalid_header,
                0,
                "Primitive container directory offsets are inconsistent"
            });
    }
    if (record_count.value() > limits.max_records) {
        return core::Result<
            PrimitiveContainerIndex,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::record_limit_exceeded,
                4,
                "Primitive container record count exceeds configured limits"
            });
    }

    const auto count64 = static_cast<std::uint64_t>(record_count.value());
    if (count64 >
        std::numeric_limits<std::uint64_t>::max() / kRecordDescriptorSize) {
        return core::Result<
            PrimitiveContainerIndex,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::offset_overflow,
                4,
                "Primitive container directory size would overflow"
            });
    }
    const auto directory_size = count64 * kRecordDescriptorSize;
    if (directory_offset.value() > source.size() ||
        directory_size > source.size() - directory_offset.value()) {
        return core::Result<
            PrimitiveContainerIndex,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::truncated,
                directory_offset.value(),
                "Primitive container directory is truncated"
            });
    }

    const auto seek = reader.seek(directory_offset.value());
    if (!seek.has_value()) {
        return core::Result<
            PrimitiveContainerIndex,
            PrimitiveContainerError>::failure(
            reader_error(
                seek.error(),
                "Primitive container directory offset is invalid"));
    }

    std::vector<PrimitiveRecord> records;
    records.reserve(record_count.value());
    for (std::uint32_t index = 0; index < record_count.value(); ++index) {
        const auto descriptor_offset = reader.offset();
        const auto record_offset = reader.read_u32();
        const auto record_size = reader.read_u32();
        const auto kind = reader.read_u32();
        const auto metadata = reader.read_u32();
        if (!record_offset || !record_size || !kind || !metadata) {
            return core::Result<
                PrimitiveContainerIndex,
                PrimitiveContainerError>::failure(
                {
                    PrimitiveContainerErrorCode::truncated,
                    descriptor_offset,
                    "Primitive record descriptor is truncated"
                });
        }
        if (record_size.value() > limits.max_record_size) {
            return core::Result<
                PrimitiveContainerIndex,
                PrimitiveContainerError>::failure(
                {
                    PrimitiveContainerErrorCode::record_limit_exceeded,
                    descriptor_offset + 4U,
                    "Primitive record size exceeds configured limits"
                });
        }
        if (record_offset.value() > directory_offset.value() ||
            record_size.value() >
                directory_offset.value() - record_offset.value()) {
            return core::Result<
                PrimitiveContainerIndex,
                PrimitiveContainerError>::failure(
                {
                    PrimitiveContainerErrorCode::invalid_record,
                    descriptor_offset,
                    "Primitive record exceeds the data region"
                });
        }
        records.push_back(
            {
                record_offset.value(),
                record_size.value(),
                kind.value(),
                metadata.value()
            });
    }

    return core::Result<
        PrimitiveContainerIndex,
        PrimitiveContainerError>::success(
        PrimitiveContainerIndex(
            {
                directory_offset.value(),
                record_count.value(),
                reserved.value()
            },
            std::move(records)));
}

const PrimitiveContainerHeader&
PrimitiveContainerIndex::header() const noexcept {
    return header_;
}

const std::vector<PrimitiveRecord>&
PrimitiveContainerIndex::records() const noexcept {
    return records_;
}

core::Result<std::vector<std::byte>, PrimitiveContainerError>
PrimitiveContainerIndex::read_record(
    const datasource::IReadOnlyDataSource& source,
    std::size_t record_index,
    datasource::ReadBudget& budget) const {
    if (record_index >= records_.size()) {
        return core::Result<
            std::vector<std::byte>,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::invalid_record,
                record_index,
                "Primitive record index is out of range"
            });
    }
    const auto& record = records_[record_index];
    if (record.size >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return core::Result<
            std::vector<std::byte>,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::record_limit_exceeded,
                record.offset,
                "Primitive record cannot fit in memory"
            });
    }
    auto contents = source.read(
        record.offset,
        static_cast<std::size_t>(record.size),
        budget);
    if (!contents.has_value()) {
        return core::Result<
            std::vector<std::byte>,
            PrimitiveContainerError>::failure(
            {
                PrimitiveContainerErrorCode::source_error,
                contents.error().offset,
                contents.error().message
            });
    }
    return core::Result<
        std::vector<std::byte>,
        PrimitiveContainerError>::success(
        std::move(contents.value()));
}

}

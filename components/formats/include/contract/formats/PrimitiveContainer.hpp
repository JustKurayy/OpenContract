#pragma once

#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace contract::formats {

enum class PrimitiveContainerErrorCode {
    truncated,
    invalid_header,
    record_limit_exceeded,
    offset_overflow,
    invalid_record,
    source_error
};

struct PrimitiveContainerError {
    PrimitiveContainerErrorCode code{
        PrimitiveContainerErrorCode::invalid_header};
    std::uint64_t offset{0};
    std::string message;
};

struct PrimitiveContainerLimits {
    std::size_t max_records{65'536};
    std::uint64_t max_record_size{256U * 1024U * 1024U};
};

struct PrimitiveContainerHeader {
    std::uint64_t directory_offset{0};
    std::uint32_t record_count{0};
    std::uint32_t reserved{0};
};

struct PrimitiveRecord {
    std::uint64_t offset{0};
    std::uint64_t size{0};
    std::uint32_t kind{0};
    std::uint32_t metadata{0};
};

class PrimitiveContainerIndex {
public:
    [[nodiscard]] static core::Result<
        PrimitiveContainerIndex,
        PrimitiveContainerError>
    read(
        const datasource::IReadOnlyDataSource& source,
        datasource::ReadBudget& budget,
        PrimitiveContainerLimits limits = {});

    [[nodiscard]] const PrimitiveContainerHeader& header() const noexcept;
    [[nodiscard]] const std::vector<PrimitiveRecord>& records() const noexcept;

    [[nodiscard]] core::Result<
        std::vector<std::byte>,
        PrimitiveContainerError>
    read_record(
        const datasource::IReadOnlyDataSource& source,
        std::size_t record_index,
        datasource::ReadBudget& budget) const;

private:
    PrimitiveContainerIndex(
        PrimitiveContainerHeader header,
        std::vector<PrimitiveRecord> records);

    PrimitiveContainerHeader header_;
    std::vector<PrimitiveRecord> records_;
};

}

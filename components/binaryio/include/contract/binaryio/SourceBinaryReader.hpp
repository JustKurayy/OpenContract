#pragma once

#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace contract::binaryio {

enum class SourceBinaryErrorCode {
    end_of_source,
    overflow,
    invalid_alignment,
    source_error
};

struct SourceBinaryError {
    SourceBinaryErrorCode code{SourceBinaryErrorCode::end_of_source};
    std::uint64_t offset{0};
    std::optional<datasource::DataSourceError> source_error;
    std::string message;
};

class SourceBinaryReader {
public:
    SourceBinaryReader(
        const datasource::IReadOnlyDataSource& source,
        datasource::ReadBudget& budget);

    [[nodiscard]] std::uint64_t offset() const noexcept;
    [[nodiscard]] std::uint64_t remaining() const noexcept;

    [[nodiscard]] core::Result<std::uint8_t, SourceBinaryError> read_u8();
    [[nodiscard]] core::Result<std::uint16_t, SourceBinaryError> read_u16();
    [[nodiscard]] core::Result<std::uint32_t, SourceBinaryError> read_u32();
    [[nodiscard]] core::Result<std::uint64_t, SourceBinaryError> read_u64();
    [[nodiscard]] core::Result<float, SourceBinaryError> read_f32();
    [[nodiscard]] core::Result<double, SourceBinaryError> read_f64();
    [[nodiscard]] core::Result<std::vector<std::byte>, SourceBinaryError>
    read_bytes(std::size_t length);

    [[nodiscard]] core::Result<void, SourceBinaryError> seek(
        std::uint64_t target);
    [[nodiscard]] core::Result<void, SourceBinaryError> align(
        std::uint64_t alignment);

private:
    const datasource::IReadOnlyDataSource& source_;
    datasource::ReadBudget& budget_;
    std::uint64_t offset_{0};
};

}

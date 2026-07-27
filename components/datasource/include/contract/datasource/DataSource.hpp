#pragma once

#include <contract/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace contract::datasource {

enum class DataSourceErrorCode {
    end_of_source,
    offset_overflow,
    read_limit_exceeded,
    total_budget_exceeded,
    path_missing,
    not_a_file,
    permission_denied,
    io_error
};

struct DataSourceError {
    DataSourceErrorCode code{DataSourceErrorCode::io_error};
    std::uint64_t offset{0};
    std::string message;
};

class ReadBudget {
public:
    ReadBudget(std::uint64_t total_limit, std::size_t per_read_limit);

    [[nodiscard]] std::uint64_t total_limit() const noexcept;
    [[nodiscard]] std::size_t per_read_limit() const noexcept;
    [[nodiscard]] std::uint64_t consumed() const noexcept;
    [[nodiscard]] std::uint64_t remaining() const noexcept;

    [[nodiscard]] core::Result<void, DataSourceError> consume(
        std::uint64_t offset,
        std::size_t length);

private:
    std::uint64_t total_limit_{0};
    std::size_t per_read_limit_{0};
    std::uint64_t consumed_{0};
};

class IReadOnlyDataSource {
public:
    virtual ~IReadOnlyDataSource() = default;

    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    [[nodiscard]] virtual core::Result<std::vector<std::byte>, DataSourceError> read(
        std::uint64_t offset,
        std::size_t length,
        ReadBudget& budget) const = 0;
};

class MemoryDataSource final : public IReadOnlyDataSource {
public:
    explicit MemoryDataSource(std::span<const std::byte> bytes);

    [[nodiscard]] std::uint64_t size() const noexcept override;
    [[nodiscard]] core::Result<std::vector<std::byte>, DataSourceError> read(
        std::uint64_t offset,
        std::size_t length,
        ReadBudget& budget) const override;

private:
    std::vector<std::byte> bytes_;
};

class FileDataSource final : public IReadOnlyDataSource {
public:
    [[nodiscard]] static core::Result<FileDataSource, DataSourceError> open(
        std::filesystem::path path);

    [[nodiscard]] std::uint64_t size() const noexcept override;
    [[nodiscard]] core::Result<std::vector<std::byte>, DataSourceError> read(
        std::uint64_t offset,
        std::size_t length,
        ReadBudget& budget) const override;

private:
    FileDataSource(std::filesystem::path path, std::uint64_t size);

    std::filesystem::path path_;
    std::uint64_t size_{0};
};

}

#include <contract/binaryio/SourceBinaryReader.hpp>

#include <bit>
#include <limits>
#include <utility>

namespace contract::binaryio {
namespace {

SourceBinaryError source_error(
    const datasource::DataSourceError& error) {
    SourceBinaryErrorCode code = SourceBinaryErrorCode::source_error;
    if (error.code == datasource::DataSourceErrorCode::end_of_source) {
        code = SourceBinaryErrorCode::end_of_source;
    } else if (error.code == datasource::DataSourceErrorCode::offset_overflow) {
        code = SourceBinaryErrorCode::overflow;
    }
    return {code, error.offset, error, error.message};
}

template <typename Integer>
core::Result<Integer, SourceBinaryError> read_little_endian(
    SourceBinaryReader& reader) {
    auto bytes = reader.read_bytes(sizeof(Integer));
    if (!bytes.has_value()) {
        return core::Result<Integer, SourceBinaryError>::failure(
            bytes.error());
    }

    Integer value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        const auto byte_value = static_cast<Integer>(
            std::to_integer<unsigned int>(bytes.value()[index]));
        value |= static_cast<Integer>(byte_value << (index * 8U));
    }
    return core::Result<Integer, SourceBinaryError>::success(value);
}

}

SourceBinaryReader::SourceBinaryReader(
    const datasource::IReadOnlyDataSource& source,
    datasource::ReadBudget& budget)
    : source_(source),
      budget_(budget) {}

std::uint64_t SourceBinaryReader::offset() const noexcept {
    return offset_;
}

std::uint64_t SourceBinaryReader::remaining() const noexcept {
    return source_.size() - offset_;
}

core::Result<std::uint8_t, SourceBinaryError>
SourceBinaryReader::read_u8() {
    auto bytes = read_bytes(1);
    if (!bytes.has_value()) {
        return core::Result<std::uint8_t, SourceBinaryError>::failure(
            bytes.error());
    }
    return core::Result<std::uint8_t, SourceBinaryError>::success(
        std::to_integer<std::uint8_t>(bytes.value()[0]));
}

core::Result<std::uint16_t, SourceBinaryError>
SourceBinaryReader::read_u16() {
    return read_little_endian<std::uint16_t>(*this);
}

core::Result<std::uint32_t, SourceBinaryError>
SourceBinaryReader::read_u32() {
    return read_little_endian<std::uint32_t>(*this);
}

core::Result<std::uint64_t, SourceBinaryError>
SourceBinaryReader::read_u64() {
    return read_little_endian<std::uint64_t>(*this);
}

core::Result<float, SourceBinaryError> SourceBinaryReader::read_f32() {
    const auto bits = read_u32();
    if (!bits.has_value()) {
        return core::Result<float, SourceBinaryError>::failure(bits.error());
    }
    return core::Result<float, SourceBinaryError>::success(
        std::bit_cast<float>(bits.value()));
}

core::Result<double, SourceBinaryError> SourceBinaryReader::read_f64() {
    const auto bits = read_u64();
    if (!bits.has_value()) {
        return core::Result<double, SourceBinaryError>::failure(bits.error());
    }
    return core::Result<double, SourceBinaryError>::success(
        std::bit_cast<double>(bits.value()));
}

core::Result<std::vector<std::byte>, SourceBinaryError>
SourceBinaryReader::read_bytes(std::size_t length) {
    const auto length64 = static_cast<std::uint64_t>(length);
    if (offset_ > std::numeric_limits<std::uint64_t>::max() - length64) {
        return core::Result<
            std::vector<std::byte>,
            SourceBinaryError>::failure(
            {
                SourceBinaryErrorCode::overflow,
                offset_,
                std::nullopt,
                "Read end offset would overflow"
            });
    }
    if (length64 > remaining()) {
        return core::Result<
            std::vector<std::byte>,
            SourceBinaryError>::failure(
            {
                SourceBinaryErrorCode::end_of_source,
                offset_,
                std::nullopt,
                "Read exceeds the data source bounds"
            });
    }

    auto bytes = source_.read(offset_, length, budget_);
    if (!bytes.has_value()) {
        return core::Result<
            std::vector<std::byte>,
            SourceBinaryError>::failure(
            source_error(bytes.error()));
    }
    if (bytes.value().size() != length) {
        return core::Result<
            std::vector<std::byte>,
            SourceBinaryError>::failure(
            {
                SourceBinaryErrorCode::source_contract_violation,
                offset_,
                std::nullopt,
                "Data source returned an unexpected byte count"
            });
    }
    offset_ += length64;
    return core::Result<
        std::vector<std::byte>,
        SourceBinaryError>::success(
        std::move(bytes.value()));
}

core::Result<void, SourceBinaryError> SourceBinaryReader::seek(
    std::uint64_t target) {
    if (target > source_.size()) {
        return core::Result<void, SourceBinaryError>::failure(
            {
                SourceBinaryErrorCode::end_of_source,
                offset_,
                std::nullopt,
                "Seek target exceeds the data source bounds"
            });
    }
    offset_ = target;
    return core::Result<void, SourceBinaryError>::success();
}

core::Result<void, SourceBinaryError> SourceBinaryReader::align(
    std::uint64_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return core::Result<void, SourceBinaryError>::failure(
            {
                SourceBinaryErrorCode::invalid_alignment,
                offset_,
                std::nullopt,
                "Alignment must be a non-zero power of two"
            });
    }
    const auto mask = alignment - 1;
    if (offset_ > std::numeric_limits<std::uint64_t>::max() - mask) {
        return core::Result<void, SourceBinaryError>::failure(
            {
                SourceBinaryErrorCode::overflow,
                offset_,
                std::nullopt,
                "Aligned offset would overflow"
            });
    }
    return seek((offset_ + mask) & ~mask);
}

}

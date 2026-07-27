#pragma once

#include <contract/core/Result.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace contract::binaryio {

enum class BinaryErrorCode {
    end_of_file,
    overflow,
    invalid_alignment
};

struct BinaryError {
    BinaryErrorCode code{BinaryErrorCode::end_of_file};
    std::size_t offset{0};
    std::string message;
};

class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::byte> bytes)
        : bytes_(bytes) {}

    [[nodiscard]] std::size_t offset() const noexcept {
        return offset_;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    [[nodiscard]] core::Result<std::uint8_t, BinaryError> read_u8() {
        const auto source = read_bytes(1);
        if (!source) {
            return core::Result<std::uint8_t, BinaryError>::failure(source.error());
        }
        return core::Result<std::uint8_t, BinaryError>::success(
            std::to_integer<std::uint8_t>(source.value()[0]));
    }

    [[nodiscard]] core::Result<std::uint16_t, BinaryError> read_u16() {
        return read_little_endian<std::uint16_t>();
    }

    [[nodiscard]] core::Result<std::uint32_t, BinaryError> read_u32() {
        return read_little_endian<std::uint32_t>();
    }

    [[nodiscard]] core::Result<std::uint64_t, BinaryError> read_u64() {
        return read_little_endian<std::uint64_t>();
    }

    [[nodiscard]] core::Result<float, BinaryError> read_f32() {
        const auto bits = read_u32();
        if (!bits) {
            return core::Result<float, BinaryError>::failure(bits.error());
        }
        return core::Result<float, BinaryError>::success(std::bit_cast<float>(bits.value()));
    }

    [[nodiscard]] core::Result<std::span<const std::byte>, BinaryError> read_bytes(
        std::size_t length) {
        const auto available = ensure_available(length);
        if (!available) {
            return core::Result<std::span<const std::byte>, BinaryError>::failure(
                available.error());
        }
        const auto result = bytes_.subspan(offset_, length);
        offset_ += length;
        return core::Result<std::span<const std::byte>, BinaryError>::success(result);
    }

    [[nodiscard]] core::Result<void, BinaryError> seek(std::size_t target) {
        if (target > bytes_.size()) {
            return core::Result<void, BinaryError>::failure(
                {BinaryErrorCode::end_of_file, offset_, "Seek target exceeds input bounds"});
        }
        offset_ = target;
        return core::Result<void, BinaryError>::success();
    }

    [[nodiscard]] core::Result<void, BinaryError> align(std::size_t alignment) {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            return core::Result<void, BinaryError>::failure(
                {BinaryErrorCode::invalid_alignment, offset_,
                 "Alignment must be a non-zero power of two"});
        }

        const auto mask = alignment - 1;
        if (offset_ > std::numeric_limits<std::size_t>::max() - mask) {
            return core::Result<void, BinaryError>::failure(
                {BinaryErrorCode::overflow, offset_, "Aligned offset would overflow"});
        }
        const auto target = (offset_ + mask) & ~mask;
        return seek(target);
    }

private:
    [[nodiscard]] core::Result<void, BinaryError> ensure_available(std::size_t length) const {
        if (length > std::numeric_limits<std::size_t>::max() - offset_) {
            return core::Result<void, BinaryError>::failure(
                {BinaryErrorCode::overflow, offset_, "Read end offset would overflow"});
        }
        if (length > remaining()) {
            return core::Result<void, BinaryError>::failure(
                {BinaryErrorCode::end_of_file, offset_, "Read exceeds remaining input"});
        }
        return core::Result<void, BinaryError>::success();
    }

    template <typename Integer>
    [[nodiscard]] core::Result<Integer, BinaryError> read_little_endian() {
        const auto source = read_bytes(sizeof(Integer));
        if (!source) {
            return core::Result<Integer, BinaryError>::failure(source.error());
        }

        Integer value = 0;
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            const auto byte_value = static_cast<Integer>(
                std::to_integer<unsigned int>(source.value()[index]));
            value |= static_cast<Integer>(byte_value << (index * 8U));
        }
        return core::Result<Integer, BinaryError>::success(value);
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};

}

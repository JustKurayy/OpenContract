#pragma once

#include <contract/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace contract::formats {

enum class TextureDatabaseErrorCode {
    truncated,
    invalid_offset,
    invalid_entry,
    duplicate_identifier,
    limit_exceeded
};

struct TextureDatabaseError {
    TextureDatabaseErrorCode code{
        TextureDatabaseErrorCode::truncated};
    std::uint64_t offset{0};
    std::string message;
};

enum class TextureFormat {
    bc1,
    bc2,
    rgba8,
    unsupported
};

struct TextureRecord {
    std::uint32_t texture_id{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
    std::uint32_t mip_count{0};
    TextureFormat format{TextureFormat::unsupported};
    std::size_t first_mip_offset{0};
    std::size_t first_mip_size{0};

    [[nodiscard]] std::optional<std::span<const std::byte>>
    first_mip(std::span<const std::byte> source) const noexcept;
};

struct TextureDatabase {
    std::vector<TextureRecord> textures;

    [[nodiscard]] const TextureRecord* find(
        std::uint32_t texture_id) const noexcept;
};

struct TextureDatabaseDecodeLimits {
    std::size_t max_file_size{512U * 1024U * 1024U};
    std::size_t max_textures{2048};
    std::size_t max_mips{32};
    std::size_t max_name_length{4096};
};

class TextureDatabaseDecoder {
public:
    [[nodiscard]] static core::Result<
        TextureDatabase,
        TextureDatabaseError>
    decode(
        std::span<const std::byte> bytes,
        TextureDatabaseDecodeLimits limits = {});
};

}

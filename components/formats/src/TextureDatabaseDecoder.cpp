#include <contract/formats/TextureDatabaseDecoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace contract::formats {
namespace {

constexpr std::size_t kTextureTableEntries = 2048;
constexpr std::size_t kTextureHeaderSize = 36;

core::Result<TextureDatabase, TextureDatabaseError> failure(
    TextureDatabaseErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<
        TextureDatabase,
        TextureDatabaseError>::failure(
        {code, offset, std::move(message)});
}

std::optional<std::uint16_t> read_u16(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    if (offset > bytes.size() ||
        2U > bytes.size() - offset) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(bytes[offset]) |
        (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

std::optional<std::uint32_t> read_u32(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    if (offset > bytes.size() ||
        4U > bytes.size() - offset) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(
                     bytes[offset + index])
                 << (index * 8U);
    }
    return value;
}

std::optional<std::array<char, 4>> read_type(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    if (offset > bytes.size() ||
        4U > bytes.size() - offset) {
        return std::nullopt;
    }
    return std::array<char, 4>{
        static_cast<char>(
            std::to_integer<unsigned char>(bytes[offset + 3U])),
        static_cast<char>(
            std::to_integer<unsigned char>(bytes[offset + 2U])),
        static_cast<char>(
            std::to_integer<unsigned char>(bytes[offset + 1U])),
        static_cast<char>(
            std::to_integer<unsigned char>(bytes[offset]))
    };
}

bool type_equals(
    const std::array<char, 4>& type,
    std::string_view expected) {
    return expected.size() == type.size() &&
        std::equal(type.begin(), type.end(), expected.begin());
}

TextureFormat classify(
    const std::array<char, 4>& type) {
    if (type_equals(type, "DXT1")) {
        return TextureFormat::bc1;
    }
    if (type_equals(type, "DXT3")) {
        return TextureFormat::bc2;
    }
    if (type_equals(type, "RGBA")) {
        return TextureFormat::rgba8;
    }
    return TextureFormat::unsupported;
}

}

std::optional<std::span<const std::byte>>
TextureRecord::first_mip(
    std::span<const std::byte> source) const noexcept {
    if (first_mip_offset > source.size() ||
        first_mip_size > source.size() - first_mip_offset) {
        return std::nullopt;
    }
    return source.subspan(first_mip_offset, first_mip_size);
}

const TextureRecord* TextureDatabase::find(
    std::uint32_t texture_id) const noexcept {
    for (const auto& texture : textures) {
        if (texture.texture_id == texture_id) {
            return &texture;
        }
    }
    return nullptr;
}

core::Result<TextureDatabase, TextureDatabaseError>
TextureDatabaseDecoder::decode(
    std::span<const std::byte> bytes,
    TextureDatabaseDecodeLimits limits) {
    if (bytes.size() > limits.max_file_size) {
        return failure(
            TextureDatabaseErrorCode::limit_exceeded,
            0,
            "Texture database exceeds configured limits");
    }
    const auto table_offset = read_u32(bytes, 0);
    if (!table_offset.has_value()) {
        return failure(
            TextureDatabaseErrorCode::truncated,
            0,
            "Texture database header is truncated");
    }
    constexpr auto table_size =
        kTextureTableEntries * sizeof(std::uint32_t);
    if (table_offset.value() > bytes.size() ||
        table_size > bytes.size() - table_offset.value()) {
        return failure(
            TextureDatabaseErrorCode::invalid_offset,
            0,
            "Texture entry table exceeds the input");
    }

    TextureDatabase result;
    result.textures.reserve(
        std::min(limits.max_textures, kTextureTableEntries));
    for (std::size_t slot = 0;
         slot < kTextureTableEntries;
         ++slot) {
        const auto pointer_offset =
            static_cast<std::size_t>(table_offset.value()) +
            slot * sizeof(std::uint32_t);
        const auto record_offset = read_u32(bytes, pointer_offset);
        if (!record_offset.has_value()) {
            return failure(
                TextureDatabaseErrorCode::truncated,
                pointer_offset,
                "Texture entry pointer is truncated");
        }
        if (record_offset.value() == 0U) {
            continue;
        }
        if (result.textures.size() >= limits.max_textures) {
            return failure(
                TextureDatabaseErrorCode::limit_exceeded,
                pointer_offset,
                "Texture count exceeds configured limits");
        }
        if (record_offset.value() > bytes.size() ||
            kTextureHeaderSize >
                bytes.size() - record_offset.value()) {
            return failure(
                TextureDatabaseErrorCode::invalid_offset,
                pointer_offset,
                "Texture record offset exceeds the input");
        }
        const auto base =
            static_cast<std::size_t>(record_offset.value());
        const auto type = read_type(bytes, base + 4U);
        const auto texture_id = read_u32(bytes, base + 12U);
        const auto height = read_u16(bytes, base + 16U);
        const auto width = read_u16(bytes, base + 18U);
        const auto mip_count = read_u32(bytes, base + 20U);
        if (!type.has_value() ||
            !texture_id.has_value() ||
            !height.has_value() ||
            !width.has_value() ||
            !mip_count.has_value()) {
            return failure(
                TextureDatabaseErrorCode::truncated,
                base,
                "Texture record header is truncated");
        }
        if (width.value() == 0U ||
            height.value() == 0U ||
            mip_count.value() == 0U ||
            mip_count.value() > limits.max_mips) {
            return failure(
                TextureDatabaseErrorCode::invalid_entry,
                base + 16U,
                "Texture dimensions or mip count are invalid");
        }
        if (result.find(texture_id.value()) != nullptr) {
            return failure(
                TextureDatabaseErrorCode::duplicate_identifier,
                base + 12U,
                "Texture identifier is duplicated");
        }

        auto cursor = base + kTextureHeaderSize;
        std::size_t name_length = 0;
        while (cursor < bytes.size() &&
               bytes[cursor] != std::byte{0} &&
               name_length <= limits.max_name_length) {
            ++cursor;
            ++name_length;
        }
        if (cursor >= bytes.size()) {
            return failure(
                TextureDatabaseErrorCode::truncated,
                base + kTextureHeaderSize,
                "Texture name is not terminated");
        }
        if (name_length > limits.max_name_length) {
            return failure(
                TextureDatabaseErrorCode::limit_exceeded,
                base + kTextureHeaderSize,
                "Texture name exceeds configured limits");
        }
        ++cursor;

        std::size_t first_mip_offset = 0;
        std::size_t first_mip_size = 0;
        for (std::uint32_t mip = 0;
             mip < mip_count.value();
             ++mip) {
            const auto mip_size = read_u32(bytes, cursor);
            if (!mip_size.has_value()) {
                return failure(
                    TextureDatabaseErrorCode::truncated,
                    cursor,
                    "Texture mip size is truncated");
            }
            cursor += sizeof(std::uint32_t);
            if (mip_size.value() > bytes.size() - cursor) {
                return failure(
                    TextureDatabaseErrorCode::truncated,
                    cursor,
                    "Texture mip data is truncated");
            }
            if (mip == 0U) {
                first_mip_offset = cursor;
                first_mip_size = mip_size.value();
            }
            cursor += mip_size.value();
        }
        result.textures.push_back(
            {
                texture_id.value(),
                width.value(),
                height.value(),
                mip_count.value(),
                classify(type.value()),
                first_mip_offset,
                first_mip_size
            });
    }
    return core::Result<
        TextureDatabase,
        TextureDatabaseError>::success(std::move(result));
}

}

#include <contract/formats/MaterialDatabaseDecoder.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace contract::formats {
namespace {

constexpr std::size_t kPropertySize = 16;
constexpr std::size_t kEntryMinimumSize = 32;

core::Result<MaterialDatabase, MaterialDatabaseError> failure(
    MaterialDatabaseErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<
        MaterialDatabase,
        MaterialDatabaseError>::failure(
        {code, offset, std::move(message)});
}

std::optional<std::uint32_t> read_u32(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    if (offset > bytes.size() ||
        sizeof(std::uint32_t) > bytes.size() - offset) {
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

std::optional<std::array<char, 4>> read_tag(
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

bool tag_equals(
    const std::array<char, 4>& tag,
    std::string_view expected) {
    return expected.size() == tag.size() &&
        std::equal(tag.begin(), tag.end(), expected.begin());
}

core::Result<std::string, MaterialDatabaseError> read_string(
    std::span<const std::byte> bytes,
    std::uint32_t offset,
    std::size_t max_length) {
    if (offset >= bytes.size()) {
        return core::Result<
            std::string,
            MaterialDatabaseError>::failure(
            {
                MaterialDatabaseErrorCode::invalid_offset,
                offset,
                "Material string offset exceeds the input"
            });
    }
    const auto available = bytes.subspan(offset);
    const auto limit = std::min(
        available.size(),
        max_length + 1U);
    for (std::size_t length = 0; length < limit; ++length) {
        if (available[length] == std::byte{0}) {
            std::string value;
            value.reserve(length);
            for (std::size_t index = 0; index < length; ++index) {
                value.push_back(static_cast<char>(
                    std::to_integer<unsigned char>(
                        available[index])));
            }
            return core::Result<
                std::string,
                MaterialDatabaseError>::success(std::move(value));
        }
    }
    return core::Result<
        std::string,
        MaterialDatabaseError>::failure(
        {
            MaterialDatabaseErrorCode::limit_exceeded,
            offset,
            "Material string is unterminated or exceeds configured limits"
        });
}

core::Result<void, MaterialDatabaseError> visit_property(
    std::span<const std::byte> bytes,
    std::size_t offset,
    std::size_t depth,
    std::size_t& visited,
    MaterialDefinition& material,
    const MaterialDatabaseDecodeLimits& limits) {
    if (depth > limits.max_depth ||
        visited >= limits.max_properties) {
        return core::Result<void, MaterialDatabaseError>::failure(
            {
                MaterialDatabaseErrorCode::limit_exceeded,
                offset,
                "Material property tree exceeds configured limits"
            });
    }
    if (offset > bytes.size() ||
        kPropertySize > bytes.size() - offset) {
        return core::Result<void, MaterialDatabaseError>::failure(
            {
                MaterialDatabaseErrorCode::truncated,
                offset,
                "Material property is truncated"
            });
    }
    ++visited;
    const auto tag = read_tag(bytes, offset);
    const auto data = read_u32(bytes, offset + 4U);
    const auto count = read_u32(bytes, offset + 8U);
    const auto type = read_u32(bytes, offset + 12U);
    if (!tag.has_value() ||
        !data.has_value() ||
        !count.has_value() ||
        !type.has_value()) {
        return core::Result<void, MaterialDatabaseError>::failure(
            {
                MaterialDatabaseErrorCode::truncated,
                offset,
                "Material property is truncated"
            });
    }
    if (type.value() > 3U) {
        return core::Result<void, MaterialDatabaseError>::failure(
            {
                MaterialDatabaseErrorCode::invalid_property,
                offset + 12U,
                "Material property type is unsupported"
            });
    }
    if (type.value() != 3U) {
        return core::Result<void, MaterialDatabaseError>::success();
    }
    if (count.value() >
        (bytes.size() - std::min<std::size_t>(
             data.value(),
             bytes.size())) /
            kPropertySize ||
        data.value() > bytes.size()) {
        return core::Result<void, MaterialDatabaseError>::failure(
            {
                MaterialDatabaseErrorCode::invalid_offset,
                offset + 4U,
                "Material child property range exceeds the input"
            });
    }

    if (tag_equals(tag.value(), "TEXT")) {
        std::optional<std::string> texture_name;
        std::optional<std::uint32_t> texture_id;
        for (std::uint32_t index = 0;
             index < count.value();
             ++index) {
            const auto child_offset =
                static_cast<std::size_t>(data.value()) +
                static_cast<std::size_t>(index) * kPropertySize;
            const auto child_tag = read_tag(bytes, child_offset);
            const auto child_data = read_u32(bytes, child_offset + 4U);
            const auto child_type = read_u32(bytes, child_offset + 12U);
            if (!child_tag.has_value() ||
                !child_data.has_value() ||
                !child_type.has_value()) {
                return core::Result<void, MaterialDatabaseError>::failure(
                    {
                        MaterialDatabaseErrorCode::truncated,
                        child_offset,
                        "Material texture property is truncated"
                    });
            }
            if (tag_equals(child_tag.value(), "NAME") &&
                child_type.value() == 1U) {
                auto value = read_string(
                    bytes,
                    child_data.value(),
                    limits.max_string_length);
                if (!value.has_value()) {
                    return core::Result<
                        void,
                        MaterialDatabaseError>::failure(value.error());
                }
                texture_name = std::move(value.value());
            } else if (
                tag_equals(child_tag.value(), "TXID") &&
                child_type.value() == 2U) {
                texture_id = child_data.value();
            }
        }
        if (texture_name == "mapDiffuse" &&
            texture_id.has_value()) {
            material.diffuse_texture_id = texture_id;
        }
    }
    if (tag_equals(tag.value(), "INST")) {
        for (std::uint32_t index = 0;
             index < count.value();
             ++index) {
            const auto child_offset =
                static_cast<std::size_t>(data.value()) +
                static_cast<std::size_t>(index) * kPropertySize;
            const auto child_tag = read_tag(bytes, child_offset);
            const auto child_data = read_u32(bytes, child_offset + 4U);
            const auto child_type = read_u32(bytes, child_offset + 12U);
            if (!child_tag.has_value() ||
                !child_data.has_value() ||
                !child_type.has_value()) {
                return core::Result<void, MaterialDatabaseError>::failure(
                    {
                        MaterialDatabaseErrorCode::truncated,
                        child_offset,
                        "Material instance property is truncated"
                    });
            }
            if (tag_equals(child_tag.value(), "NAME") &&
                child_type.value() == 1U) {
                auto value = read_string(
                    bytes,
                    child_data.value(),
                    limits.max_string_length);
                if (!value.has_value()) {
                    return core::Result<
                        void,
                        MaterialDatabaseError>::failure(value.error());
                }
                material.name = std::move(value.value());
                auto normalized = material.name;
                std::transform(
                    normalized.begin(),
                    normalized.end(),
                    normalized.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(
                            std::tolower(character));
                    });
                material.collision_only =
                    normalized.find("_glacier") != std::string::npos ||
                    normalized.find("_test") != std::string::npos;
                material.overlay_only =
                    normalized.find("screens") != std::string::npos;
                break;
            }
        }
    }
    if (tag_equals(tag.value(), "RSTA")) {
        for (std::uint32_t index = 0;
             index < count.value();
             ++index) {
            const auto child_offset =
                static_cast<std::size_t>(data.value()) +
                static_cast<std::size_t>(index) * kPropertySize;
            const auto child_tag = read_tag(bytes, child_offset);
            const auto child_data = read_u32(bytes, child_offset + 4U);
            const auto child_type = read_u32(bytes, child_offset + 12U);
            if (!child_tag.has_value() ||
                !child_data.has_value() ||
                !child_type.has_value()) {
                return core::Result<void, MaterialDatabaseError>::failure(
                    {
                        MaterialDatabaseErrorCode::truncated,
                        child_offset,
                        "Material render-state property is truncated"
                    });
            }
            if (tag_equals(child_tag.value(), "BENA") &&
                child_type.value() == 2U) {
                material.blend_enabled = child_data.value() != 0U;
            } else if (
                tag_equals(child_tag.value(), "ATST") &&
                child_type.value() == 2U) {
                material.alpha_test_enabled =
                    child_data.value() != 0U;
            } else if (
                tag_equals(child_tag.value(), "AREF") &&
                child_type.value() == 2U) {
                material.alpha_reference =
                    static_cast<std::uint8_t>(
                        std::min(child_data.value(), 255U));
            } else if (
                tag_equals(child_tag.value(), "OPAC") &&
                child_type.value() == 0U) {
                const auto opacity =
                    std::bit_cast<float>(child_data.value());
                if (!std::isfinite(opacity)) {
                    return core::Result<
                        void,
                        MaterialDatabaseError>::failure(
                        {
                            MaterialDatabaseErrorCode::invalid_property,
                            child_offset + 4U,
                            "Material opacity is not finite"
                        });
                }
                material.opacity = std::clamp(opacity, 0.0F, 1.0F);
            } else if (
                (tag_equals(child_tag.value(), "CULL") ||
                 tag_equals(child_tag.value(), "BMOD")) &&
                child_type.value() == 1U) {
                auto value = read_string(
                    bytes,
                    child_data.value(),
                    limits.max_string_length);
                if (!value.has_value()) {
                    return core::Result<
                        void,
                        MaterialDatabaseError>::failure(value.error());
                }
                if (tag_equals(child_tag.value(), "CULL")) {
                    if (value.value() == "TwoSided") {
                        material.cull_mode =
                            MaterialCullMode::two_sided;
                    } else if (value.value() == "OneSided") {
                        material.cull_mode =
                            MaterialCullMode::one_sided;
                    }
                } else if (value.value() == "ADD") {
                    material.blend_mode =
                        MaterialBlendMode::additive;
                } else {
                    material.blend_mode =
                        MaterialBlendMode::alpha;
                }
            }
        }
        if (!material.blend_enabled) {
            material.blend_mode = MaterialBlendMode::opaque;
        }
    }

    for (std::uint32_t index = 0;
         index < count.value();
         ++index) {
        const auto child_offset =
            static_cast<std::size_t>(data.value()) +
            static_cast<std::size_t>(index) * kPropertySize;
        auto child = visit_property(
            bytes,
            child_offset,
            depth + 1U,
            visited,
            material,
            limits);
        if (!child.has_value()) {
            return child;
        }
    }
    return core::Result<void, MaterialDatabaseError>::success();
}

}

const MaterialDefinition* MaterialDatabase::find(
    std::uint16_t material_id) const noexcept {
    if (material_id == 0U ||
        material_id > materials.size()) {
        return nullptr;
    }
    return &materials[material_id - 1U];
}

core::Result<MaterialDatabase, MaterialDatabaseError>
MaterialDatabaseDecoder::decode(
    std::span<const std::byte> bytes,
    MaterialDatabaseDecodeLimits limits) {
    if (bytes.size() > limits.max_file_size) {
        return failure(
            MaterialDatabaseErrorCode::limit_exceeded,
            0,
            "Material database exceeds configured limits");
    }
    const auto entries_offset = read_u32(bytes, 4);
    if (!entries_offset.has_value()) {
        return failure(
            MaterialDatabaseErrorCode::truncated,
            0,
            "Material database header is truncated");
    }
    if (entries_offset.value() > bytes.size() ||
        8U > bytes.size() - entries_offset.value()) {
        return failure(
            MaterialDatabaseErrorCode::invalid_offset,
            4,
            "Material entry table offset exceeds the input");
    }

    MaterialDatabase result;
    std::size_t visited = 0;
    for (std::size_t material_index = 1;
         material_index <= limits.max_materials;
         ++material_index) {
        const auto pointer_offset =
            static_cast<std::size_t>(entries_offset.value()) +
            material_index * sizeof(std::uint32_t);
        const auto entry_offset = read_u32(bytes, pointer_offset);
        if (!entry_offset.has_value()) {
            return failure(
                MaterialDatabaseErrorCode::truncated,
                pointer_offset,
                "Material entry table is not terminated");
        }
        if (entry_offset.value() == 0U) {
            return core::Result<
                MaterialDatabase,
                MaterialDatabaseError>::success(std::move(result));
        }
        if (entry_offset.value() > bytes.size() ||
            kEntryMinimumSize >
                bytes.size() - entry_offset.value()) {
            return failure(
                MaterialDatabaseErrorCode::invalid_offset,
                pointer_offset,
                "Material entry offset exceeds the input");
        }
        const auto instance_offset = read_u32(
            bytes,
            static_cast<std::size_t>(entry_offset.value()) + 28U);
        if (!instance_offset.has_value()) {
            return failure(
                MaterialDatabaseErrorCode::truncated,
                entry_offset.value(),
                "Material entry header is truncated");
        }
        MaterialDefinition material;
        material.material_id =
            static_cast<std::uint16_t>(material_index);
        auto parsed = visit_property(
            bytes,
            instance_offset.value(),
            0,
            visited,
            material,
            limits);
        if (!parsed.has_value()) {
            return core::Result<
                MaterialDatabase,
                MaterialDatabaseError>::failure(parsed.error());
        }
        result.materials.push_back(std::move(material));
    }
    return failure(
        MaterialDatabaseErrorCode::limit_exceeded,
        entries_offset.value(),
        "Material entry table exceeds configured limits");
}

}

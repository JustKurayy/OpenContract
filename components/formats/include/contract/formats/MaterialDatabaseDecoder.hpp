#pragma once

#include <contract/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace contract::formats {

enum class MaterialDatabaseErrorCode {
    truncated,
    invalid_offset,
    invalid_property,
    limit_exceeded
};

struct MaterialDatabaseError {
    MaterialDatabaseErrorCode code{
        MaterialDatabaseErrorCode::truncated};
    std::uint64_t offset{0};
    std::string message;
};

struct MaterialDatabaseDecodeLimits {
    std::size_t max_file_size{64U * 1024U * 1024U};
    std::size_t max_materials{65'535};
    std::size_t max_properties{1'000'000};
    std::size_t max_depth{32};
    std::size_t max_string_length{1024};
};

enum class MaterialCullMode {
    unspecified,
    one_sided,
    two_sided
};

enum class MaterialBlendMode {
    opaque,
    alpha,
    additive
};

struct MaterialDefinition {
    std::uint16_t material_id{0};
    std::string name;
    std::optional<std::uint32_t> diffuse_texture_id;
    bool collision_only{false};
    bool overlay_only{false};
    bool blend_enabled{false};
    bool alpha_test_enabled{false};
    std::uint8_t alpha_reference{0};
    MaterialCullMode cull_mode{MaterialCullMode::unspecified};
    MaterialBlendMode blend_mode{MaterialBlendMode::opaque};
    float opacity{1.0F};
};

struct MaterialDatabase {
    std::vector<MaterialDefinition> materials;

    [[nodiscard]] const MaterialDefinition* find(
        std::uint16_t material_id) const noexcept;
};

class MaterialDatabaseDecoder {
public:
    [[nodiscard]] static core::Result<
        MaterialDatabase,
        MaterialDatabaseError>
    decode(
        std::span<const std::byte> bytes,
        MaterialDatabaseDecodeLimits limits = {});
};

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace contract::scene {

struct RenderVertex {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float u{0.0F};
    float v{0.0F};
};

enum class RenderTextureFormat {
    bc1,
    bc2,
    rgba8
};

struct RenderTexture {
    std::uint32_t source_id{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
    RenderTextureFormat format{RenderTextureFormat::rgba8};
    std::vector<std::byte> data;
};

enum class RenderBlendMode {
    opaque,
    alpha,
    additive
};

enum class RenderCullMode {
    unspecified,
    one_sided,
    two_sided
};

struct RenderBatch {
    std::uint32_t first_index{0};
    std::uint32_t index_count{0};
    std::uint16_t source_material_id{0};
    std::optional<std::size_t> texture_index;
    RenderBlendMode blend_mode{RenderBlendMode::opaque};
    RenderCullMode cull_mode{RenderCullMode::unspecified};
    float opacity{1.0F};
    float alpha_reference{0.0F};
};

struct RenderScene {
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderTexture> textures;
    std::vector<RenderBatch> batches;
    std::size_t source_mesh_count{0};
};

}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

enum class RenderLayer {
    world,
    background
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
    RenderLayer layer{RenderLayer::world};
};

struct RenderSkinning {
    std::array<std::uint16_t, 4> joints{0, 0, 0, 0};
    std::array<float, 4> weights{1.0F, 0.0F, 0.0F, 0.0F};
};

enum class RenderJointRole {
    unknown,
    root,
    pelvis,
    spine_lower,
    spine_middle,
    spine_upper,
    neck,
    head,
    left_clavicle,
    left_upper_arm,
    left_forearm,
    left_hand,
    right_clavicle,
    right_upper_arm,
    right_forearm,
    right_hand,
    left_thigh,
    left_calf,
    left_foot,
    left_toe,
    right_thigh,
    right_calf,
    right_foot,
    right_toe
};

struct RenderJoint {
    std::string name;
    std::optional<std::size_t> parent_index;
    RenderJointRole role{RenderJointRole::unknown};
    std::array<float, 3> reference_position{0.0F, 0.0F, 0.0F};
    std::array<float, 9> reference_basis{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F
    };
};

struct RenderSkeleton {
    std::vector<RenderJoint> joints;
};

struct RenderScene {
    std::vector<RenderVertex> vertices;
    std::vector<RenderSkinning> skinning;
    std::vector<std::uint32_t> indices;
    std::vector<RenderTexture> textures;
    std::vector<RenderBatch> batches;
    std::optional<RenderSkeleton> skeleton;
    std::size_t source_mesh_count{0};
};

}

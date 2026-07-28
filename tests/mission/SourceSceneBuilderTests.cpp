#include "TestSupport.hpp"

#include <contract/formats/GmsSceneDecoder.hpp>
#include <contract/formats/MaterialDatabaseDecoder.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ScenePlacementDecoder.hpp>
#include <contract/formats/TextureDatabaseDecoder.hpp>
#include <contract/mission/SourceSceneBuilder.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

int main() {
    contract::formats::PrimitiveMesh mesh;
    mesh.model_record = 42;
    mesh.material_id = 77;
    mesh.positions = {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}
    };
    mesh.indices = {0, 1, 2};
    mesh.texture_coordinates = {
        {0.0F, 0.0F},
        {1.0F, 0.0F},
        {0.0F, 1.0F}
    };

    contract::formats::ScenePlacement placement;
    placement.primitive_record = 42;
    placement.matrix = {
        0.0F, 1.0F, 0.0F,
        -1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F
    };
    placement.position = {10.0F, 20.0F, 30.0F};

    auto second = placement;
    second.position = {100.0F, 200.0F, 300.0F};

    auto inactive = placement;
    inactive.inactive = true;

    auto invisible = placement;
    invisible.invisible = true;

    auto missing = placement;
    missing.primitive_record = 99;

    const std::vector meshes{mesh};
    const std::vector placements{
        placement,
        second,
        inactive,
        invisible,
        missing
    };
    const auto built =
        contract::mission::SourceSceneBuilder::build(
            meshes,
            placements);
    CONTRACT_EXPECT(built.has_value());
    if (built.has_value()) {
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.vertices.size(),
            std::size_t{6});
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.indices.size(),
            std::size_t{6});
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.source_mesh_count,
            std::size_t{2});
        CONTRACT_EXPECT_EQ(
            built.value().active_placements,
            std::size_t{2});
        CONTRACT_EXPECT_EQ(
            built.value().inactive_placements,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            built.value().invisible_placements,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            built.value().missing_placements,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.vertices[0].x,
            10.0F);
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.vertices[0].y,
            21.0F);
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.vertices[0].z,
            30.0F);
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.vertices[3].x,
            100.0F);
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.vertices[3].y,
            201.0F);
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.vertices[3].z,
            300.0F);
        CONTRACT_EXPECT_EQ(
            built.value().render_scene.indices[3],
            std::uint32_t{3});
    }

    contract::formats::ScenePlacement root;
    root.primitive_record = 0;
    root.position = {10.0F, 0.0F, 0.0F};
    contract::formats::ScenePlacement child;
    child.primitive_record = 42;
    child.position = {0.0F, 5.0F, 0.0F};
    contract::formats::ScenePlacement hidden_parent;
    hidden_parent.primitive_record = 0;
    hidden_parent.inactive = true;
    contract::formats::ScenePlacement hidden_child;
    hidden_child.primitive_record = 42;

    std::vector<contract::formats::GmsSceneNode> hierarchy(4);
    hierarchy[0].name = "root";
    hierarchy[1].name = "spawn.synthetic";
    hierarchy[1].parent_index = 0;
    hierarchy[2].name = "hidden-group";
    hierarchy[2].parent_index = 0;
    hierarchy[2].visibility_group_root = true;
    hierarchy[3].name = "hidden-child";
    hierarchy[3].parent_index = 2;

    const std::vector nested_placements{
        root,
        child,
        hidden_parent,
        hidden_child
    };
    contract::formats::MaterialDatabase materials;
    materials.materials.resize(77);
    for (std::size_t index = 0;
         index < materials.materials.size();
         ++index) {
        materials.materials[index].material_id =
            static_cast<std::uint16_t>(index + 1U);
    }
    materials.materials.back().diffuse_texture_id = 123;
    materials.materials.back().blend_enabled = true;
    materials.materials.back().blend_mode =
        contract::formats::MaterialBlendMode::alpha;
    materials.materials.back().cull_mode =
        contract::formats::MaterialCullMode::two_sided;
    materials.materials.back().opacity = 0.25F;
    materials.materials.back().alpha_test_enabled = true;
    materials.materials.back().alpha_reference = 127;
    materials.materials[1].collision_only = true;
    materials.materials[2].overlay_only = true;
    contract::formats::TextureDatabase textures;
    textures.textures.push_back(
        {
            123,
            4,
            4,
            1,
            contract::formats::TextureFormat::bc1,
            0,
            8
        });
    const std::vector<std::byte> texture_bytes(
        8,
        std::byte{0x5a});
    const contract::mission::SourceSceneBuildResources resources{
        &materials,
        &textures,
        texture_bytes
    };
    auto collision_mesh = mesh;
    collision_mesh.material_id = 2;
    auto overlay_mesh = mesh;
    overlay_mesh.material_id = 3;
    const std::vector nested_meshes{
        mesh,
        collision_mesh,
        overlay_mesh
    };
    constexpr std::string_view preferred_spawn_nodes[]{
        "spawn.synthetic"};
    const auto nested =
        contract::mission::SourceSceneBuilder::build(
            nested_meshes,
            nested_placements,
            hierarchy,
            resources,
            {preferred_spawn_nodes});
    CONTRACT_EXPECT(nested.has_value());
    if (nested.has_value()) {
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.vertices[0].x,
            11.0F);
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.vertices[0].y,
            5.0F);
        CONTRACT_EXPECT_EQ(
            nested.value().inherited_inactive_placements,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            nested.value().visibility_group_count,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.source_mesh_count,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            nested.value().collision_meshes,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            nested.value().overlay_meshes,
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.vertices[1].u,
            1.0F);
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.textures.size(),
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.batches.size(),
            std::size_t{1});
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.batches[0].texture_index.value(),
            std::size_t{0});
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.batches[0].source_material_id,
            std::uint16_t{77});
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.batches[0].blend_mode,
            contract::scene::RenderBlendMode::alpha);
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.batches[0].cull_mode,
            contract::scene::RenderCullMode::two_sided);
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.batches[0].opacity,
            0.25F);
        CONTRACT_EXPECT_EQ(
            nested.value().render_scene.textures[0].data.size(),
            std::size_t{8});
        CONTRACT_EXPECT(nested.value().preferred_spawn.has_value());
        CONTRACT_EXPECT_EQ(
            nested.value().preferred_spawn->position[0],
            10.0F);
        CONTRACT_EXPECT_EQ(
            nested.value().preferred_spawn->position[1],
            5.0F);
        CONTRACT_EXPECT_EQ(
            nested.value().preferred_spawn->position[2],
            0.0F);
    }

    return contract::test::finish();
}

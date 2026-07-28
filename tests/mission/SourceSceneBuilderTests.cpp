#include "TestSupport.hpp"

#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ScenePlacementDecoder.hpp>
#include <contract/mission/SourceSceneBuilder.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    contract::formats::PrimitiveMesh mesh;
    mesh.model_record = 42;
    mesh.positions = {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}
    };
    mesh.indices = {0, 1, 2};

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

    auto missing = placement;
    missing.primitive_record = 99;

    const std::vector meshes{mesh};
    const std::vector placements{
        placement,
        second,
        inactive,
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

    return contract::test::finish();
}

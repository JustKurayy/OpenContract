#include "TestSupport.hpp"

#include <contract/rendering/BgfxRenderer.hpp>
#include <contract/rendering/RenderBatchPolicy.hpp>
#include <contract/rendering/SceneFraming.hpp>
#include <contract/rendering/WireframeIndexBuilder.hpp>

#include <array>
#include <cstdint>

int main() {
    using namespace contract;

    rendering::BgfxRenderer renderer;
    CONTRACT_EXPECT(!renderer.initialized());

    const std::array<scene::RenderBatch, 4> unordered_batches{
        scene::RenderBatch{
            .source_material_id = 10,
            .blend_mode = scene::RenderBlendMode::alpha},
        scene::RenderBatch{
            .source_material_id = 11,
            .layer = scene::RenderLayer::background},
        scene::RenderBatch{
            .source_material_id = 12},
        scene::RenderBatch{
            .source_material_id = 13,
            .blend_mode = scene::RenderBlendMode::additive}
    };
    const auto ordered_batches =
        rendering::order_render_batches(unordered_batches);
    CONTRACT_EXPECT_EQ(
        ordered_batches[0].source_material_id,
        std::uint16_t{11});
    CONTRACT_EXPECT_EQ(
        ordered_batches[1].source_material_id,
        std::uint16_t{12});
    CONTRACT_EXPECT_EQ(
        ordered_batches[2].source_material_id,
        std::uint16_t{10});
    CONTRACT_EXPECT_EQ(
        ordered_batches[3].source_material_id,
        std::uint16_t{13});
    CONTRACT_EXPECT(
        !rendering::render_batch_writes_depth(
            ordered_batches[0]));
    CONTRACT_EXPECT(
        rendering::render_batch_writes_depth(
            ordered_batches[1]));
    CONTRACT_EXPECT(
        !rendering::render_batch_writes_depth(
            ordered_batches[2]));

    const std::array<std::uint32_t, 6> triangles{
        0, 1, 2,
        2, 3, 0};
    const auto wireframe =
        rendering::build_wireframe_indices(triangles);
    CONTRACT_EXPECT(wireframe.has_value());
    CONTRACT_EXPECT_EQ(
        wireframe.value(),
        (std::vector<std::uint32_t>{
            0, 1, 1, 2, 2, 0,
            2, 3, 3, 0, 0, 2}));

    const std::array<std::uint32_t, 2> incomplete{0, 1};
    const auto rejected =
        rendering::build_wireframe_indices(incomplete);
    CONTRACT_EXPECT(!rejected.has_value());
    CONTRACT_EXPECT_EQ(
        rejected.error().code,
        rendering::WireframeIndexErrorCode::incomplete_triangle);

    scene::RenderScene scene;
    scene.vertices = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F}};
    scene.indices = {0, 1, 2};
    const auto upload_before_initialize = renderer.upload_scene(scene);
    CONTRACT_EXPECT(!upload_before_initialize.has_value());
    CONTRACT_EXPECT_EQ(
        upload_before_initialize.error().code,
        rendering::RendererErrorCode::not_initialized);
    const auto player_upload_before_initialize =
        renderer.upload_player_model(scene);
    CONTRACT_EXPECT(!player_upload_before_initialize.has_value());
    CONTRACT_EXPECT_EQ(
        player_upload_before_initialize.error().code,
        rendering::RendererErrorCode::not_initialized);

    const auto missing_window = renderer.initialize(
        nullptr,
        std::uint32_t{960},
        std::uint32_t{540});
    CONTRACT_EXPECT(!missing_window.has_value());
    CONTRACT_EXPECT_EQ(
        missing_window.error().code,
        rendering::RendererErrorCode::invalid_native_window);
    CONTRACT_EXPECT(!renderer.initialized());

    const auto missing_width = renderer.initialize(
        reinterpret_cast<void*>(std::uintptr_t{1}),
        std::uint32_t{0},
        std::uint32_t{540});
    CONTRACT_EXPECT(!missing_width.has_value());
    CONTRACT_EXPECT_EQ(
        missing_width.error().code,
        rendering::RendererErrorCode::invalid_dimensions);
    CONTRACT_EXPECT(!renderer.initialized());

    const auto oversized = renderer.initialize(
        reinterpret_cast<void*>(std::uintptr_t{1}),
        std::uint32_t{65536},
        std::uint32_t{540});
    CONTRACT_EXPECT(!oversized.has_value());
    CONTRACT_EXPECT_EQ(
        oversized.error().code,
        rendering::RendererErrorCode::invalid_dimensions);
    CONTRACT_EXPECT(!renderer.initialized());

    scene::RenderScene framed_scene;
    framed_scene.vertices = {
        {-100.0F, -100.0F, -100.0F},
        {100.0F, 100.0F, 100.0F},
        {-1.0F, -1.0F, -1.0F},
        {1.0F, 1.0F, 1.0F}
    };
    framed_scene.indices = {0, 1, 0, 2, 3, 2};
    framed_scene.batches = {
        {0, 3, 1, std::nullopt},
        {3, 3, 2, std::size_t{0}}
    };
    framed_scene.textures.push_back(
        {1, 1, 1, scene::RenderTextureFormat::rgba8, {}});
    const auto frame =
        rendering::choose_initial_scene_frame(framed_scene);
    CONTRACT_EXPECT(frame.has_value());
    if (frame.has_value()) {
        CONTRACT_EXPECT_EQ(frame->center.x, 0.0F);
        CONTRACT_EXPECT_EQ(frame->center.y, 0.0F);
        CONTRACT_EXPECT_EQ(frame->center.z, 0.0F);
        CONTRACT_EXPECT(frame->radius < 2.0F);
    }

    scene::RenderScene robust_scene;
    for (int value = -10; value < 10; ++value) {
        const auto coordinate = static_cast<float>(value);
        robust_scene.vertices.push_back(
            {coordinate, coordinate, coordinate});
        robust_scene.indices.push_back(
            static_cast<std::uint32_t>(
                robust_scene.indices.size()));
    }
    robust_scene.vertices.push_back(
        {-1000.0F, -1000.0F, -1000.0F});
    robust_scene.vertices.push_back(
        {1000.0F, 1000.0F, 1000.0F});
    robust_scene.indices.push_back(20);
    robust_scene.indices.push_back(21);
    robust_scene.batches = {
        {
            0,
            static_cast<std::uint32_t>(
                robust_scene.indices.size()),
            1,
            std::size_t{0}
        }
    };
    robust_scene.textures.push_back(
        {1, 1, 1, scene::RenderTextureFormat::rgba8, {}});
    const auto robust_frame =
        rendering::choose_initial_scene_frame(robust_scene);
    CONTRACT_EXPECT(robust_frame.has_value());
    if (robust_frame.has_value()) {
        CONTRACT_EXPECT(robust_frame->radius < 20.0F);
    }

    return test::finish();
}

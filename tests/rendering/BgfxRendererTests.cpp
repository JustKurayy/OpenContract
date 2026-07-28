#include "TestSupport.hpp"

#include <contract/rendering/BgfxRenderer.hpp>
#include <contract/rendering/WireframeIndexBuilder.hpp>

#include <array>
#include <cstdint>

int main() {
    using namespace contract;

    rendering::BgfxRenderer renderer;
    CONTRACT_EXPECT(!renderer.initialized());

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

    return test::finish();
}

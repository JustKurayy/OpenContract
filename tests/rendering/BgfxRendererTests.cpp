#include "TestSupport.hpp"

#include <contract/rendering/BgfxRenderer.hpp>

#include <cstdint>

int main() {
    using namespace contract;

    rendering::BgfxRenderer renderer;
    CONTRACT_EXPECT(!renderer.initialized());

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

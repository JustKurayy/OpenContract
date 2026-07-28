#include <contract/rendering/BgfxRenderer.hpp>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace contract::rendering {
namespace {

core::Result<void, RendererError> renderer_failure(
    RendererErrorCode code,
    std::string message) {
    return core::Result<void, RendererError>::failure(
        {code, std::move(message)});
}

bool valid_dimensions(
    std::uint32_t width,
    std::uint32_t height) noexcept {
    constexpr auto maximum =
        std::numeric_limits<std::uint16_t>::max();
    return width > 0 &&
           height > 0 &&
           width <= maximum &&
           height <= maximum;
}

}

BgfxRenderer::~BgfxRenderer() {
    shutdown();
}

core::Result<void, RendererError> BgfxRenderer::initialize(
    void* native_window,
    std::uint32_t width,
    std::uint32_t height) {
    if (native_window == nullptr) {
        return renderer_failure(
            RendererErrorCode::invalid_native_window,
            "A native window handle is required");
    }
    if (!valid_dimensions(width, height)) {
        return renderer_failure(
            RendererErrorCode::invalid_dimensions,
            "Renderer dimensions must be between 1 and 65535");
    }
    if (initialized_) {
        return renderer_failure(
            RendererErrorCode::initialization_failed,
            "The renderer is already initialized");
    }

    bgfx::Init init;
    init.type = bgfx::RendererType::Count;
    init.vendorId = BGFX_PCI_ID_NONE;
    init.platformData.nwh = native_window;
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        return renderer_failure(
            RendererErrorCode::initialization_failed,
            "bgfx failed to initialize a supported rendering backend");
    }

    initialized_ = true;
    width_ = width;
    height_ = height;
    backend_name_ = bgfx::getRendererName(bgfx::getRendererType());
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    bgfx::setViewClear(
        0,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        0x14171dff,
        1.0F,
        0);
    return core::Result<void, RendererError>::success();
}

core::Result<void, RendererError> BgfxRenderer::resize(
    std::uint32_t width,
    std::uint32_t height) {
    if (!initialized_) {
        return renderer_failure(
            RendererErrorCode::not_initialized,
            "The renderer is not initialized");
    }
    if (!valid_dimensions(width, height)) {
        return renderer_failure(
            RendererErrorCode::invalid_dimensions,
            "Renderer dimensions must be between 1 and 65535");
    }

    width_ = width;
    height_ = height;
    bgfx::reset(width_, height_, BGFX_RESET_VSYNC);
    return core::Result<void, RendererError>::success();
}

core::Result<void, RendererError> BgfxRenderer::render(
    const runtime::RuntimeObservation& observation) {
    if (!initialized_) {
        return renderer_failure(
            RendererErrorCode::not_initialized,
            "The renderer is not initialized");
    }

    bgfx::setViewRect(
        0,
        0,
        0,
        static_cast<std::uint16_t>(width_),
        static_cast<std::uint16_t>(height_));
    bgfx::touch(0);
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(
        3,
        2,
        0x0f,
        "OpenContract - bgfx %s",
        backend_name_.c_str());
    bgfx::dbgTextPrintf(
        3,
        4,
        0x0e,
        "Mission: %s",
        observation.mission.value().c_str());
    bgfx::dbgTextPrintf(
        3,
        5,
        0x0e,
        "Map: %s",
        observation.map.value().c_str());
    bgfx::dbgTextPrintf(
        3,
        7,
        0x0f,
        "Simulation tick: %llu",
        static_cast<unsigned long long>(
            observation.completed_ticks));
    bgfx::dbgTextPrintf(
        3,
        8,
        0x0f,
        "Entities: %llu",
        static_cast<unsigned long long>(
            observation.entities.size()));
    bgfx::dbgTextPrintf(
        3,
        9,
        0x0f,
        "Objectives: %llu",
        static_cast<unsigned long long>(
            observation.objectives.size()));
    bgfx::dbgTextPrintf(
        3,
        11,
        0x08,
        "Close the window or press Escape to exit.");
    static_cast<void>(bgfx::frame());
    return core::Result<void, RendererError>::success();
}

void BgfxRenderer::shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    bgfx::shutdown();
    initialized_ = false;
    width_ = 0;
    height_ = 0;
    backend_name_.clear();
}

bool BgfxRenderer::initialized() const noexcept {
    return initialized_;
}

std::string_view BgfxRenderer::backend_name() const noexcept {
    return backend_name_;
}

}

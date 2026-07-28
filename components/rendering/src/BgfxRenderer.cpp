#include <contract/rendering/BgfxRenderer.hpp>
#include <contract/rendering/WireframeIndexBuilder.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "EmbeddedShaders.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace contract::rendering {
namespace {

struct GpuVertex {
    float x;
    float y;
    float z;
    std::uint32_t color;
};

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

bgfx::ProgramHandle program_handle(std::uint16_t index) {
    return {index};
}

bgfx::VertexBufferHandle vertex_buffer_handle(std::uint16_t index) {
    return {index};
}

bgfx::IndexBufferHandle index_buffer_handle(std::uint16_t index) {
    return {index};
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
    init.type = bgfx::RendererType::Direct3D11;
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

    const auto vertex_shader = bgfx::createShader(
        bgfx::copy(
            contract_vertex_shader,
            sizeof(contract_vertex_shader)));
    const auto fragment_shader = bgfx::createShader(
        bgfx::copy(
            contract_fragment_shader,
            sizeof(contract_fragment_shader)));
    if (!bgfx::isValid(vertex_shader) ||
        !bgfx::isValid(fragment_shader)) {
        if (bgfx::isValid(vertex_shader)) {
            bgfx::destroy(vertex_shader);
        }
        if (bgfx::isValid(fragment_shader)) {
            bgfx::destroy(fragment_shader);
        }
        shutdown();
        return renderer_failure(
            RendererErrorCode::resource_creation_failed,
            "Could not create scene shaders");
    }
    const auto program = bgfx::createProgram(
        vertex_shader,
        fragment_shader,
        true);
    if (!bgfx::isValid(program)) {
        shutdown();
        return renderer_failure(
            RendererErrorCode::resource_creation_failed,
            "Could not create the scene shader program");
    }
    program_handle_ = program.idx;
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

core::Result<void, RendererError> BgfxRenderer::upload_scene(
    const scene::RenderScene& scene) {
    if (!initialized_) {
        return renderer_failure(
            RendererErrorCode::not_initialized,
            "The renderer is not initialized");
    }
    if (scene.vertices.empty() || scene.indices.empty() ||
        scene.vertices.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        scene.indices.size() >
            std::numeric_limits<std::uint32_t>::max()) {
        return renderer_failure(
            RendererErrorCode::invalid_scene,
            "Render scene must contain addressable vertices and indices");
    }
    for (const auto index : scene.indices) {
        if (index >= scene.vertices.size()) {
            return renderer_failure(
                RendererErrorCode::invalid_scene,
                "Render scene contains an out-of-range index");
        }
    }
    float minimum_x = scene.vertices.front().x;
    float minimum_y = scene.vertices.front().y;
    float minimum_z = scene.vertices.front().z;
    float maximum_x = minimum_x;
    float maximum_y = minimum_y;
    float maximum_z = minimum_z;
    for (const auto& vertex : scene.vertices) {
        if (!std::isfinite(vertex.x) ||
            !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z)) {
            return renderer_failure(
                RendererErrorCode::invalid_scene,
                "Render scene contains a non-finite vertex");
        }
        minimum_x = std::min(minimum_x, vertex.x);
        minimum_y = std::min(minimum_y, vertex.y);
        minimum_z = std::min(minimum_z, vertex.z);
        maximum_x = std::max(maximum_x, vertex.x);
        maximum_y = std::max(maximum_y, vertex.y);
        maximum_z = std::max(maximum_z, vertex.z);
    }
    const auto scene_center_x = (minimum_x + maximum_x) * 0.5F;
    const auto scene_center_y = (minimum_y + maximum_y) * 0.5F;
    const auto scene_center_z = (minimum_z + maximum_z) * 0.5F;
    const auto extent_x = maximum_x - minimum_x;
    const auto extent_y = maximum_y - minimum_y;
    const auto extent_z = maximum_z - minimum_z;
    scene_radius_ = std::max(
        1.0F,
        0.5F * std::sqrt(
            extent_x * extent_x +
            extent_y * extent_y +
            extent_z * extent_z));
    camera_.frame_scene(
        {
            scene_center_x,
            scene_center_y,
            scene_center_z
        },
        scene_radius_);

    std::vector<GpuVertex> gpu_vertices;
    gpu_vertices.reserve(scene.vertices.size());
    const auto height_range = std::max(1.0F, extent_y);
    for (const auto& vertex : scene.vertices) {
        const auto height =
            std::clamp((vertex.y - minimum_y) / height_range, 0.0F, 1.0F);
        const auto red = static_cast<std::uint32_t>(45.0F + height * 35.0F);
        const auto green =
            static_cast<std::uint32_t>(105.0F + height * 105.0F);
        const auto blue =
            static_cast<std::uint32_t>(95.0F + height * 55.0F);
        const auto color =
            0xff000000U |
            (blue << 16U) |
            (green << 8U) |
            red;
        gpu_vertices.push_back(
            {vertex.x, vertex.y, vertex.z, color});
    }

    const auto vertex_bytes =
        gpu_vertices.size() * sizeof(GpuVertex);
    const auto index_bytes =
        scene.indices.size() * sizeof(std::uint32_t);
    auto wireframe_indices =
        build_wireframe_indices(scene.indices);
    if (!wireframe_indices.has_value()) {
        return renderer_failure(
            RendererErrorCode::invalid_scene,
            "Render scene contains an incomplete triangle");
    }
    const auto wireframe_index_bytes =
        wireframe_indices.value().size() * sizeof(std::uint32_t);
    if (vertex_bytes > std::numeric_limits<std::uint32_t>::max() ||
        index_bytes > std::numeric_limits<std::uint32_t>::max() ||
        wireframe_index_bytes >
            std::numeric_limits<std::uint32_t>::max()) {
        return renderer_failure(
            RendererErrorCode::invalid_scene,
            "Render scene buffer size exceeds bgfx limits");
    }

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(
            bgfx::Attrib::Color0,
            4,
            bgfx::AttribType::Uint8,
            true)
        .end();
    const auto vertices = bgfx::createVertexBuffer(
        bgfx::copy(
            gpu_vertices.data(),
            static_cast<std::uint32_t>(vertex_bytes)),
        layout);
    const auto indices = bgfx::createIndexBuffer(
        bgfx::copy(
            scene.indices.data(),
            static_cast<std::uint32_t>(index_bytes)),
        BGFX_BUFFER_INDEX32);
    const auto wireframe_indices_handle = bgfx::createIndexBuffer(
        bgfx::copy(
            wireframe_indices.value().data(),
            static_cast<std::uint32_t>(wireframe_index_bytes)),
        BGFX_BUFFER_INDEX32);
    if (!bgfx::isValid(vertices) ||
        !bgfx::isValid(indices) ||
        !bgfx::isValid(wireframe_indices_handle)) {
        if (bgfx::isValid(vertices)) {
            bgfx::destroy(vertices);
        }
        if (bgfx::isValid(indices)) {
            bgfx::destroy(indices);
        }
        if (bgfx::isValid(wireframe_indices_handle)) {
            bgfx::destroy(wireframe_indices_handle);
        }
        return renderer_failure(
            RendererErrorCode::resource_creation_failed,
            "Could not create scene geometry buffers");
    }

    if (vertex_buffer_handle_ != 0xffffU) {
        bgfx::destroy(vertex_buffer_handle(vertex_buffer_handle_));
    }
    if (index_buffer_handle_ != 0xffffU) {
        bgfx::destroy(index_buffer_handle(index_buffer_handle_));
    }
    if (wireframe_index_buffer_handle_ != 0xffffU) {
        bgfx::destroy(
            index_buffer_handle(wireframe_index_buffer_handle_));
    }
    vertex_buffer_handle_ = vertices.idx;
    index_buffer_handle_ = indices.idx;
    wireframe_index_buffer_handle_ =
        wireframe_indices_handle.idx;
    vertex_count_ = static_cast<std::uint32_t>(scene.vertices.size());
    index_count_ = static_cast<std::uint32_t>(scene.indices.size());
    wireframe_index_count_ = static_cast<std::uint32_t>(
        wireframe_indices.value().size());
    return core::Result<void, RendererError>::success();
}

core::Result<void, RendererError> BgfxRenderer::render(
    const runtime::RuntimeObservation& observation,
    const FreeCameraInput& camera_input,
    float elapsed_seconds,
    bool wireframe) {
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
    if (vertex_buffer_handle_ != 0xffffU &&
        index_buffer_handle_ != 0xffffU &&
        program_handle_ != 0xffffU) {
        camera_.update(camera_input, elapsed_seconds);
        const auto camera_position = camera_.position();
        const auto camera_target = camera_.target();
        const bx::Vec3 target{
            camera_target.x,
            camera_target.y,
            camera_target.z};
        const bx::Vec3 eye{
            camera_position.x,
            camera_position.y,
            camera_position.z};
        float view[16];
        bx::mtxLookAt(view, eye, target);
        float projection[16];
        bx::mtxProj(
            projection,
            60.0F,
            static_cast<float>(width_) /
                static_cast<float>(height_),
            std::max(0.1F, scene_radius_ * 0.001F),
            scene_radius_ * 5.0F,
            bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(0, view, projection);
        bgfx::setVertexBuffer(
            0,
            vertex_buffer_handle(vertex_buffer_handle_));
        const auto selected_index_buffer =
            wireframe
                ? wireframe_index_buffer_handle_
                : index_buffer_handle_;
        bgfx::setIndexBuffer(
            index_buffer_handle(selected_index_buffer));
        auto render_state =
            BGFX_STATE_WRITE_RGB |
            BGFX_STATE_WRITE_A |
            BGFX_STATE_WRITE_Z |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_MSAA;
        if (wireframe) {
            render_state |= BGFX_STATE_PT_LINES;
        }
        bgfx::setState(render_state);
        bgfx::submit(0, program_handle(program_handle_));
    }
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
        10,
        0x0f,
        "Geometry: %u vertices, %u indices",
        vertex_count_,
        wireframe ? wireframe_index_count_ : index_count_);
    bgfx::dbgTextPrintf(
        3,
        12,
        0x0b,
        "Camera: %.1f %.1f %.1f",
        camera_.position().x,
        camera_.position().y,
        camera_.position().z);
    bgfx::dbgTextPrintf(
        3,
        14,
        0x08,
        "WASD move, Q/E down/up, arrows look, Shift boosts.");
    bgfx::dbgTextPrintf(
        3,
        15,
        0x08,
        "F1 toggles %s view. Escape exits.",
        wireframe ? "solid" : "wireframe");
    static_cast<void>(bgfx::frame());
    return core::Result<void, RendererError>::success();
}

void BgfxRenderer::shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    if (vertex_buffer_handle_ != 0xffffU) {
        bgfx::destroy(vertex_buffer_handle(vertex_buffer_handle_));
    }
    if (index_buffer_handle_ != 0xffffU) {
        bgfx::destroy(index_buffer_handle(index_buffer_handle_));
    }
    if (wireframe_index_buffer_handle_ != 0xffffU) {
        bgfx::destroy(
            index_buffer_handle(wireframe_index_buffer_handle_));
    }
    if (program_handle_ != 0xffffU) {
        bgfx::destroy(program_handle(program_handle_));
    }
    bgfx::shutdown();
    initialized_ = false;
    width_ = 0;
    height_ = 0;
    backend_name_.clear();
    program_handle_ = 0xffffU;
    vertex_buffer_handle_ = 0xffffU;
    index_buffer_handle_ = 0xffffU;
    wireframe_index_buffer_handle_ = 0xffffU;
    vertex_count_ = 0;
    index_count_ = 0;
    wireframe_index_count_ = 0;
    scene_radius_ = 1.0F;
    camera_ = FreeCamera{};
}

bool BgfxRenderer::initialized() const noexcept {
    return initialized_;
}

std::string_view BgfxRenderer::backend_name() const noexcept {
    return backend_name_;
}

}

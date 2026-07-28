#include <contract/rendering/BgfxRenderer.hpp>
#include <contract/rendering/ProceduralCharacter.hpp>
#include <contract/rendering/SceneFraming.hpp>
#include <contract/rendering/WireframeIndexBuilder.hpp>
#include <contract/runtime/PlayerController.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "fs_contract_texture.sc.bin.h"
#include "vs_contract_texture.sc.bin.h"

#include <algorithm>
#include <array>
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
    float u;
    float v;
};

bgfx::VertexLayout gpu_vertex_layout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

std::vector<GpuVertex> gpu_vertices(
    const std::vector<scene::RenderVertex>& vertices) {
    std::vector<GpuVertex> converted;
    converted.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        converted.push_back(
            {
                vertex.x,
                vertex.y,
                vertex.z,
                vertex.u,
                vertex.v
            });
    }
    return converted;
}

const runtime::RuntimeEntityObservation* find_player(
    const runtime::RuntimeObservation& observation) {
    for (const auto& entity : observation.entities) {
        if (!entity.enabled) {
            continue;
        }
        const auto component = std::find_if(
            entity.components.begin(),
            entity.components.end(),
            [](const scene::ComponentReference& reference) {
                return reference.type == runtime::player_component_type;
            });
        if (component != entity.components.end()) {
            return &entity;
        }
    }
    return nullptr;
}

std::array<float, 16> model_transform(
    const scene::Transform& transform) {
    const auto x = transform.rotation[0];
    const auto y = transform.rotation[1];
    const auto z = transform.rotation[2];
    const auto w = transform.rotation[3];
    const auto xx = x * x;
    const auto yy = y * y;
    const auto zz = z * z;
    const auto xy = x * y;
    const auto xz = x * z;
    const auto yz = y * z;
    const auto wx = w * x;
    const auto wy = w * y;
    const auto wz = w * z;
    return {
        (1.0F - 2.0F * (yy + zz)) * transform.scale[0],
        (2.0F * (xy + wz)) * transform.scale[0],
        (2.0F * (xz - wy)) * transform.scale[0],
        0.0F,
        (2.0F * (xy - wz)) * transform.scale[1],
        (1.0F - 2.0F * (xx + zz)) * transform.scale[1],
        (2.0F * (yz + wx)) * transform.scale[1],
        0.0F,
        (2.0F * (xz + wy)) * transform.scale[2],
        (2.0F * (yz - wx)) * transform.scale[2],
        (1.0F - 2.0F * (xx + yy)) * transform.scale[2],
        0.0F,
        transform.position[0],
        transform.position[1],
        transform.position[2],
        1.0F
    };
}

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

bgfx::TextureHandle texture_handle(std::uint16_t index) {
    return {index};
}

bgfx::UniformHandle uniform_handle(std::uint16_t index) {
    return {index};
}

bgfx::TextureFormat::Enum texture_format(
    scene::RenderTextureFormat format) {
    switch (format) {
    case scene::RenderTextureFormat::bc1:
        return bgfx::TextureFormat::BC1;
    case scene::RenderTextureFormat::bc2:
        return bgfx::TextureFormat::BC2;
    case scene::RenderTextureFormat::rgba8:
        return bgfx::TextureFormat::RGBA8;
    }
    return bgfx::TextureFormat::RGBA8;
}

std::optional<std::size_t> expected_texture_size(
    const scene::RenderTexture& texture) {
    const auto width = static_cast<std::size_t>(texture.width);
    const auto height = static_cast<std::size_t>(texture.height);
    if (texture.format == scene::RenderTextureFormat::rgba8) {
        if (width > std::numeric_limits<std::size_t>::max() / height ||
            width * height >
                std::numeric_limits<std::size_t>::max() / 4U) {
            return std::nullopt;
        }
        return width * height * 4U;
    }
    const auto blocks_wide = (width + 3U) / 4U;
    const auto blocks_high = (height + 3U) / 4U;
    if (blocks_wide >
        std::numeric_limits<std::size_t>::max() / blocks_high) {
        return std::nullopt;
    }
    const auto blocks = blocks_wide * blocks_high;
    const auto block_size =
        texture.format == scene::RenderTextureFormat::bc1
            ? 8U
            : 16U;
    if (blocks >
        std::numeric_limits<std::size_t>::max() / block_size) {
        return std::nullopt;
    }
    return blocks * block_size;
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
            vs_contract_texture_dxbc,
            sizeof(vs_contract_texture_dxbc)));
    const auto fragment_shader = bgfx::createShader(
        bgfx::copy(
            fs_contract_texture_dxbc,
            sizeof(fs_contract_texture_dxbc)));
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
    const auto sampler = bgfx::createUniform(
        "s_texColor",
        bgfx::UniformType::Sampler);
    const auto material_uniform = bgfx::createUniform(
        "u_material",
        bgfx::UniformType::Vec4);
    const std::uint32_t white_pixel = 0xffff'ffffU;
    const auto white_texture = bgfx::createTexture2D(
        1,
        1,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_NONE,
        bgfx::copy(&white_pixel, sizeof(white_pixel)));
    if (!bgfx::isValid(sampler) ||
        !bgfx::isValid(material_uniform) ||
        !bgfx::isValid(white_texture)) {
        if (bgfx::isValid(sampler)) {
            bgfx::destroy(sampler);
        }
        if (bgfx::isValid(white_texture)) {
            bgfx::destroy(white_texture);
        }
        if (bgfx::isValid(material_uniform)) {
            bgfx::destroy(material_uniform);
        }
        shutdown();
        return renderer_failure(
            RendererErrorCode::resource_creation_failed,
            "Could not create scene texture resources");
    }
    sampler_handle_ = sampler.idx;
    material_uniform_handle_ = material_uniform.idx;
    white_texture_handle_ = white_texture.idx;

    const auto character = create_procedural_character();
    const auto character_vertices = gpu_vertices(character.vertices);
    const auto character_vertex_buffer = bgfx::createVertexBuffer(
        bgfx::copy(
            character_vertices.data(),
            static_cast<std::uint32_t>(
                character_vertices.size() * sizeof(GpuVertex))),
        gpu_vertex_layout());
    const auto character_index_buffer = bgfx::createIndexBuffer(
        bgfx::copy(
            character.indices.data(),
            static_cast<std::uint32_t>(
                character.indices.size() * sizeof(std::uint32_t))),
        BGFX_BUFFER_INDEX32);
    constexpr std::array<std::uint8_t, 4> character_pixel{
        255U,
        80U,
        24U,
        255U
    };
    const auto character_texture = bgfx::createTexture2D(
        1,
        1,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_NONE,
        bgfx::copy(
            character_pixel.data(),
            static_cast<std::uint32_t>(character_pixel.size())));
    if (!bgfx::isValid(character_vertex_buffer) ||
        !bgfx::isValid(character_index_buffer) ||
        !bgfx::isValid(character_texture)) {
        if (bgfx::isValid(character_vertex_buffer)) {
            bgfx::destroy(character_vertex_buffer);
        }
        if (bgfx::isValid(character_index_buffer)) {
            bgfx::destroy(character_index_buffer);
        }
        if (bgfx::isValid(character_texture)) {
            bgfx::destroy(character_texture);
        }
        shutdown();
        return renderer_failure(
            RendererErrorCode::resource_creation_failed,
            "Could not create procedural character resources");
    }
    character_vertex_buffer_handle_ = character_vertex_buffer.idx;
    character_index_buffer_handle_ = character_index_buffer.idx;
    character_texture_handle_ = character_texture.idx;
    character_index_count_ =
        static_cast<std::uint32_t>(character.indices.size());
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
    for (const auto& batch : scene.batches) {
        if (batch.first_index > scene.indices.size() ||
            batch.index_count >
                scene.indices.size() - batch.first_index ||
            (batch.texture_index.has_value() &&
             batch.texture_index.value() >= scene.textures.size())) {
            return renderer_failure(
                RendererErrorCode::invalid_scene,
                "Render scene contains an invalid draw batch");
        }
        if (!std::isfinite(batch.opacity) ||
            !std::isfinite(batch.alpha_reference) ||
            batch.opacity < 0.0F ||
            batch.opacity > 1.0F ||
            batch.alpha_reference < 0.0F ||
            batch.alpha_reference > 1.0F) {
            return renderer_failure(
                RendererErrorCode::invalid_scene,
                "Render scene contains invalid material state");
        }
    }
    for (const auto& texture : scene.textures) {
        const auto expected = expected_texture_size(texture);
        if (texture.width == 0U ||
            texture.height == 0U ||
            !expected.has_value() ||
            expected.value() != texture.data.size() ||
            texture.data.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            return renderer_failure(
                RendererErrorCode::invalid_scene,
                "Render scene contains invalid texture data");
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
    const auto extent_x = maximum_x - minimum_x;
    const auto extent_y = maximum_y - minimum_y;
    const auto extent_z = maximum_z - minimum_z;
    scene_radius_ = std::max(
        1.0F,
        0.5F * std::sqrt(
            extent_x * extent_x +
            extent_y * extent_y +
            extent_z * extent_z));
    const auto initial_frame = choose_initial_scene_frame(scene);
    if (!initial_frame.has_value()) {
        return renderer_failure(
            RendererErrorCode::invalid_scene,
            "Render scene has no finite camera framing bounds");
    }
    camera_.frame_scene(
        initial_frame->center,
        initial_frame->radius);

    const auto converted_vertices = gpu_vertices(scene.vertices);

    const auto vertex_bytes =
        converted_vertices.size() * sizeof(GpuVertex);
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

    const auto vertices = bgfx::createVertexBuffer(
        bgfx::copy(
            converted_vertices.data(),
            static_cast<std::uint32_t>(vertex_bytes)),
        gpu_vertex_layout());
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

    std::vector<std::uint16_t> new_texture_handles;
    new_texture_handles.reserve(scene.textures.size());
    for (const auto& texture : scene.textures) {
        const auto handle = bgfx::createTexture2D(
            texture.width,
            texture.height,
            false,
            1,
            texture_format(texture.format),
            BGFX_SAMPLER_MIN_ANISOTROPIC |
                BGFX_SAMPLER_MAG_ANISOTROPIC,
            bgfx::copy(
                texture.data.data(),
                static_cast<std::uint32_t>(texture.data.size())));
        if (!bgfx::isValid(handle)) {
            for (const auto created : new_texture_handles) {
                bgfx::destroy(texture_handle(created));
            }
            bgfx::destroy(vertices);
            bgfx::destroy(indices);
            bgfx::destroy(wireframe_indices_handle);
            return renderer_failure(
                RendererErrorCode::resource_creation_failed,
                "Could not create a scene texture");
        }
        new_texture_handles.push_back(handle.idx);
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
    for (const auto handle : texture_handles_) {
        bgfx::destroy(texture_handle(handle));
    }
    vertex_buffer_handle_ = vertices.idx;
    index_buffer_handle_ = indices.idx;
    wireframe_index_buffer_handle_ =
        wireframe_indices_handle.idx;
    texture_handles_ = std::move(new_texture_handles);
    batches_ = scene.batches;
    if (batches_.empty()) {
        batches_.push_back(
            {
                0,
                static_cast<std::uint32_t>(scene.indices.size()),
                0,
                std::nullopt
            });
    }
    textured_batch_count_ = static_cast<std::uint32_t>(
        std::count_if(
            batches_.begin(),
            batches_.end(),
            [](const scene::RenderBatch& batch) {
                return batch.texture_index.has_value();
            }));
    vertex_count_ = static_cast<std::uint32_t>(scene.vertices.size());
    index_count_ = static_cast<std::uint32_t>(scene.indices.size());
    wireframe_index_count_ = static_cast<std::uint32_t>(
        wireframe_indices.value().size());
    following_player_ = false;
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
        const auto* player = find_player(observation);
        if (player != nullptr) {
            const CameraPoint subject{
                player->transform.position[0],
                player->transform.position[1] + 90.0F,
                player->transform.position[2]
            };
            if (!following_player_) {
                camera_.frame_subject(subject, 500.0F);
                following_player_ = true;
            }
            camera_.orbit_subject(
                subject,
                camera_input,
                elapsed_seconds);
        } else {
            following_player_ = false;
            camera_.update(camera_input, elapsed_seconds);
        }
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
        auto render_state =
            BGFX_STATE_WRITE_RGB |
            BGFX_STATE_WRITE_A |
            BGFX_STATE_WRITE_Z |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_MSAA;
        if (wireframe) {
            render_state |= BGFX_STATE_PT_LINES;
            bgfx::setVertexBuffer(
                0,
                vertex_buffer_handle(vertex_buffer_handle_));
            bgfx::setIndexBuffer(
                index_buffer_handle(
                    wireframe_index_buffer_handle_));
            bgfx::setTexture(
                0,
                uniform_handle(sampler_handle_),
                texture_handle(white_texture_handle_));
            const float material[4]{1.0F, 0.0F, 0.0F, 0.0F};
            bgfx::setUniform(
                uniform_handle(material_uniform_handle_),
                material);
            bgfx::setState(render_state);
            bgfx::submit(0, program_handle(program_handle_));
        } else {
            for (const auto& batch : batches_) {
                auto batch_state = render_state;
                if (batch.blend_mode ==
                    scene::RenderBlendMode::alpha) {
                    batch_state |= BGFX_STATE_BLEND_ALPHA;
                } else if (
                    batch.blend_mode ==
                    scene::RenderBlendMode::additive) {
                    batch_state |= BGFX_STATE_BLEND_ADD;
                }
                if (batch.cull_mode ==
                    scene::RenderCullMode::one_sided) {
                    batch_state |= BGFX_STATE_CULL_CW;
                }
                bgfx::setVertexBuffer(
                    0,
                    vertex_buffer_handle(vertex_buffer_handle_));
                bgfx::setIndexBuffer(
                    index_buffer_handle(index_buffer_handle_),
                    batch.first_index,
                    batch.index_count);
                const auto texture =
                    batch.texture_index.has_value()
                        ? texture_handles_[
                              batch.texture_index.value()]
                        : white_texture_handle_;
                bgfx::setTexture(
                    0,
                    uniform_handle(sampler_handle_),
                    texture_handle(texture));
                const float material[4]{
                    batch.opacity,
                    batch.alpha_reference,
                    0.0F,
                    0.0F
                };
                bgfx::setUniform(
                    uniform_handle(material_uniform_handle_),
                    material);
                bgfx::setState(batch_state);
                bgfx::submit(0, program_handle(program_handle_));
            }
        }
        if (player != nullptr &&
            character_vertex_buffer_handle_ != 0xffffU &&
            character_index_buffer_handle_ != 0xffffU &&
            character_texture_handle_ != 0xffffU) {
            const auto transform = model_transform(player->transform);
            bgfx::setTransform(transform.data());
            bgfx::setVertexBuffer(
                0,
                vertex_buffer_handle(
                    character_vertex_buffer_handle_));
            bgfx::setIndexBuffer(
                index_buffer_handle(
                    character_index_buffer_handle_),
                0,
                character_index_count_);
            bgfx::setTexture(
                0,
                uniform_handle(sampler_handle_),
                texture_handle(character_texture_handle_));
            const float material[4]{1.0F, 0.0F, 0.0F, 0.0F};
            bgfx::setUniform(
                uniform_handle(material_uniform_handle_),
                material);
            bgfx::setState(
                BGFX_STATE_WRITE_RGB |
                BGFX_STATE_WRITE_A |
                BGFX_STATE_WRITE_Z |
                BGFX_STATE_DEPTH_TEST_LESS |
                BGFX_STATE_MSAA);
            bgfx::submit(0, program_handle(program_handle_));
        }
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
    const auto* displayed_player = find_player(observation);
    if (displayed_player != nullptr) {
        bgfx::dbgTextPrintf(
            3,
            6,
            0x0b,
            "Player: %.1f %.1f %.1f",
            displayed_player->transform.position[0],
            displayed_player->transform.position[1],
            displayed_player->transform.position[2]);
    }
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
        13,
        observation.all_objectives_complete ? 0x0a : 0x0e,
        "Mission state: %s",
        observation.all_objectives_complete
            ? "objective complete"
            : "active");
    bgfx::dbgTextPrintf(
        3,
        10,
        0x0f,
        "Geometry: %u vertices, %u indices",
        vertex_count_,
        wireframe ? wireframe_index_count_ : index_count_);
    bgfx::dbgTextPrintf(
        3,
        11,
        0x0f,
        "Materials: %u textured batches, %u textures",
        textured_batch_count_,
        static_cast<unsigned int>(texture_handles_.size()));
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
        displayed_player != nullptr
            ? "WASD move character, arrows orbit, Shift sprints."
            : "WASD move, Q/E down/up, arrows look, Shift boosts.");
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
    if (character_vertex_buffer_handle_ != 0xffffU) {
        bgfx::destroy(
            vertex_buffer_handle(character_vertex_buffer_handle_));
    }
    if (character_index_buffer_handle_ != 0xffffU) {
        bgfx::destroy(
            index_buffer_handle(character_index_buffer_handle_));
    }
    if (program_handle_ != 0xffffU) {
        bgfx::destroy(program_handle(program_handle_));
    }
    if (sampler_handle_ != 0xffffU) {
        bgfx::destroy(uniform_handle(sampler_handle_));
    }
    if (material_uniform_handle_ != 0xffffU) {
        bgfx::destroy(uniform_handle(material_uniform_handle_));
    }
    if (white_texture_handle_ != 0xffffU) {
        bgfx::destroy(texture_handle(white_texture_handle_));
    }
    if (character_texture_handle_ != 0xffffU) {
        bgfx::destroy(texture_handle(character_texture_handle_));
    }
    for (const auto handle : texture_handles_) {
        bgfx::destroy(texture_handle(handle));
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
    character_vertex_buffer_handle_ = 0xffffU;
    character_index_buffer_handle_ = 0xffffU;
    sampler_handle_ = 0xffffU;
    material_uniform_handle_ = 0xffffU;
    white_texture_handle_ = 0xffffU;
    character_texture_handle_ = 0xffffU;
    texture_handles_.clear();
    batches_.clear();
    vertex_count_ = 0;
    index_count_ = 0;
    wireframe_index_count_ = 0;
    character_index_count_ = 0;
    textured_batch_count_ = 0;
    scene_radius_ = 1.0F;
    following_player_ = false;
    camera_ = FreeCamera{};
}

bool BgfxRenderer::initialized() const noexcept {
    return initialized_;
}

std::string_view BgfxRenderer::backend_name() const noexcept {
    return backend_name_;
}

}

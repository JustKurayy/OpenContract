#pragma once

#include <contract/core/Result.hpp>
#include <contract/rendering/CharacterAnimation.hpp>
#include <contract/rendering/FreeCamera.hpp>
#include <contract/runtime/RuntimeObservation.hpp>
#include <contract/scene/RenderScene.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace contract::rendering {

enum class RendererErrorCode {
    invalid_native_window,
    invalid_dimensions,
    initialization_failed,
    not_initialized,
    invalid_scene,
    resource_creation_failed
};

struct RendererError {
    RendererErrorCode code{RendererErrorCode::initialization_failed};
    std::string message;
};

class BgfxRenderer {
public:
    BgfxRenderer() = default;
    ~BgfxRenderer();

    BgfxRenderer(const BgfxRenderer&) = delete;
    BgfxRenderer& operator=(const BgfxRenderer&) = delete;
    BgfxRenderer(BgfxRenderer&&) = delete;
    BgfxRenderer& operator=(BgfxRenderer&&) = delete;

    [[nodiscard]] core::Result<void, RendererError> initialize(
        void* native_window,
        std::uint32_t width,
        std::uint32_t height);

    [[nodiscard]] core::Result<void, RendererError> resize(
        std::uint32_t width,
        std::uint32_t height);

    [[nodiscard]] core::Result<void, RendererError> upload_scene(
        const scene::RenderScene& scene);

    [[nodiscard]] core::Result<void, RendererError> upload_player_model(
        const scene::RenderScene& scene);

    [[nodiscard]] core::Result<void, RendererError> render(
        const runtime::RuntimeObservation& observation,
        const FreeCameraInput& camera_input,
        float elapsed_seconds,
        bool wireframe,
        const CharacterAnimationState& character_animation = {});

    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::string_view backend_name() const noexcept;

private:
    bool initialized_{false};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    std::string backend_name_;
    std::uint16_t program_handle_{0xffffU};
    std::uint16_t vertex_buffer_handle_{0xffffU};
    std::uint16_t index_buffer_handle_{0xffffU};
    std::uint16_t wireframe_index_buffer_handle_{0xffffU};
    std::uint16_t character_vertex_buffer_handle_{0xffffU};
    std::uint16_t character_index_buffer_handle_{0xffffU};
    std::uint16_t sampler_handle_{0xffffU};
    std::uint16_t material_uniform_handle_{0xffffU};
    std::uint16_t white_texture_handle_{0xffffU};
    std::uint16_t character_texture_handle_{0xffffU};
    std::vector<std::uint16_t> texture_handles_;
    std::vector<std::uint16_t> character_texture_handles_;
    std::vector<scene::RenderBatch> batches_;
    std::vector<scene::RenderBatch> character_batches_;
    std::vector<scene::RenderVertex> character_base_vertices_;
    std::vector<scene::RenderSkinning> character_skinning_;
    std::optional<scene::RenderSkeleton> character_skeleton_;
    std::uint32_t vertex_count_{0};
    std::uint32_t index_count_{0};
    std::uint32_t wireframe_index_count_{0};
    std::uint32_t character_index_count_{0};
    std::uint32_t textured_batch_count_{0};
    float scene_radius_{1.0F};
    bool following_player_{false};
    bool source_character_model_{false};
    FreeCamera camera_;
};

}

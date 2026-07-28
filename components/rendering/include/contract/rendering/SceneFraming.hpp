#pragma once

#include <contract/rendering/FreeCamera.hpp>
#include <contract/scene/RenderScene.hpp>

#include <optional>

namespace contract::rendering {

struct SceneFrame {
    CameraPoint center;
    float radius{1.0F};
};

[[nodiscard]] std::optional<SceneFrame> choose_initial_scene_frame(
    const scene::RenderScene& scene) noexcept;

}

#pragma once

#include <array>

namespace contract::rendering {

struct CameraPoint {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct FreeCameraInput {
    float forward{0.0F};
    float right{0.0F};
    float up{0.0F};
    float yaw{0.0F};
    float pitch{0.0F};
    bool fast{false};
};

[[nodiscard]] float advance_camera_yaw(
    float yaw,
    float input,
    float elapsed_seconds) noexcept;

[[nodiscard]] float camera_yaw_from_rotation(
    std::array<float, 4> rotation) noexcept;

class FreeCamera {
public:
    void frame_scene(
        CameraPoint center,
        float radius) noexcept;

    void frame_subject(
        CameraPoint subject,
        float distance,
        float yaw = 0.0F) noexcept;

    void orbit_subject(
        CameraPoint subject,
        const FreeCameraInput& input,
        float elapsed_seconds) noexcept;

    void update(
        const FreeCameraInput& input,
        float elapsed_seconds) noexcept;

    [[nodiscard]] CameraPoint position() const noexcept;
    [[nodiscard]] CameraPoint target() const noexcept;
    [[nodiscard]] float yaw() const noexcept;
    [[nodiscard]] float pitch() const noexcept;

private:
    [[nodiscard]] CameraPoint forward_vector() const noexcept;

    CameraPoint position_{0.0F, 0.0F, -5.0F};
    float yaw_{0.0F};
    float pitch_{0.0F};
    float movement_speed_{1.0F};
    float focus_distance_{5.0F};
};

}

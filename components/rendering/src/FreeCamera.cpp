#include <contract/rendering/FreeCamera.hpp>

#include <algorithm>
#include <cmath>

namespace contract::rendering {
namespace {

constexpr float kMaximumPitch = 1.553343F;
constexpr float kLookSpeed = 1.5F;

float finite_delta(float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

}

float advance_camera_yaw(
    float yaw,
    float input,
    float elapsed_seconds) noexcept {
    if (!std::isfinite(yaw) ||
        !std::isfinite(input) ||
        !std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0.0F) {
        return yaw;
    }
    const auto step = std::min(elapsed_seconds, 0.25F);
    return yaw +
        std::clamp(input, -1.0F, 1.0F) *
            kLookSpeed * step;
}

float camera_yaw_from_rotation(
    std::array<float, 4> rotation) noexcept {
    const auto length = std::sqrt(
        rotation[0] * rotation[0] +
        rotation[1] * rotation[1] +
        rotation[2] * rotation[2] +
        rotation[3] * rotation[3]);
    if (!std::isfinite(length) || length <= 0.000001F) {
        return 0.0F;
    }
    for (auto& value : rotation) {
        value /= length;
    }
    const auto x = rotation[0];
    const auto y = rotation[1];
    const auto z = rotation[2];
    const auto w = rotation[3];
    return std::atan2(
        2.0F * (x * z + w * y),
        1.0F - 2.0F * (x * x + y * y));
}

void FreeCamera::frame_scene(
    CameraPoint center,
    float radius) noexcept {
    const auto safe_radius =
        std::isfinite(radius) ? std::max(radius, 1.0F) : 1.0F;
    position_ = {
        center.x,
        center.y + safe_radius * 0.35F,
        center.z - safe_radius * 1.7F
    };
    const CameraPoint to_center{
        center.x - position_.x,
        center.y - position_.y,
        center.z - position_.z
    };
    focus_distance_ = std::sqrt(
        to_center.x * to_center.x +
        to_center.y * to_center.y +
        to_center.z * to_center.z);
    yaw_ = std::atan2(to_center.x, to_center.z);
    pitch_ = std::asin(
        std::clamp(
            to_center.y / focus_distance_,
            -1.0F,
            1.0F));
    movement_speed_ = safe_radius * 0.25F;
}

void FreeCamera::frame_subject(
    CameraPoint subject,
    float distance,
    float yaw) noexcept {
    const auto safe_distance =
        std::isfinite(distance) ? std::max(distance, 1.0F) : 1.0F;
    yaw_ = std::isfinite(yaw) ? yaw : 0.0F;
    pitch_ = -0.25F;
    focus_distance_ = safe_distance;
    movement_speed_ = safe_distance * 0.25F;
    const auto forward = forward_vector();
    position_ = {
        subject.x - forward.x * focus_distance_,
        subject.y - forward.y * focus_distance_,
        subject.z - forward.z * focus_distance_
    };
}

void FreeCamera::orbit_subject(
    CameraPoint subject,
    const FreeCameraInput& input,
    float elapsed_seconds) noexcept {
    if (std::isfinite(elapsed_seconds) &&
        elapsed_seconds > 0.0F) {
        const auto step = std::min(elapsed_seconds, 0.25F);
        yaw_ = advance_camera_yaw(
            yaw_,
            input.yaw,
            elapsed_seconds) +
            finite_delta(input.yaw_delta);
        pitch_ = std::clamp(
            pitch_ +
                std::clamp(input.pitch, -1.0F, 1.0F) *
                    kLookSpeed * step +
                finite_delta(input.pitch_delta),
            -kMaximumPitch,
            kMaximumPitch);
    }
    const auto forward = forward_vector();
    position_ = {
        subject.x - forward.x * focus_distance_,
        subject.y - forward.y * focus_distance_,
        subject.z - forward.z * focus_distance_
    };
}

void FreeCamera::update(
    const FreeCameraInput& input,
    float elapsed_seconds) noexcept {
    if (!std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0.0F) {
        return;
    }
    const auto step = std::min(elapsed_seconds, 0.25F);
    yaw_ = advance_camera_yaw(
        yaw_,
        input.yaw,
        elapsed_seconds) +
        finite_delta(input.yaw_delta);
    pitch_ = std::clamp(
        pitch_ +
            std::clamp(input.pitch, -1.0F, 1.0F) *
                kLookSpeed * step +
            finite_delta(input.pitch_delta),
        -kMaximumPitch,
        kMaximumPitch);

    const auto forward = forward_vector();
    const CameraPoint right{
        std::cos(yaw_),
        0.0F,
        -std::sin(yaw_)
    };
    const auto speed =
        movement_speed_ * (input.fast ? 4.0F : 1.0F) * step;
    position_.x +=
        (forward.x * std::clamp(input.forward, -1.0F, 1.0F) +
         right.x * std::clamp(input.right, -1.0F, 1.0F)) *
        speed;
    position_.y +=
        (forward.y * std::clamp(input.forward, -1.0F, 1.0F) +
         std::clamp(input.up, -1.0F, 1.0F)) *
        speed;
    position_.z +=
        (forward.z * std::clamp(input.forward, -1.0F, 1.0F) +
         right.z * std::clamp(input.right, -1.0F, 1.0F)) *
        speed;
}

CameraPoint FreeCamera::position() const noexcept {
    return position_;
}

CameraPoint FreeCamera::target() const noexcept {
    const auto forward = forward_vector();
    return {
        position_.x + forward.x * focus_distance_,
        position_.y + forward.y * focus_distance_,
        position_.z + forward.z * focus_distance_
    };
}

float FreeCamera::yaw() const noexcept {
    return yaw_;
}

float FreeCamera::pitch() const noexcept {
    return pitch_;
}

CameraPoint FreeCamera::forward_vector() const noexcept {
    const auto horizontal = std::cos(pitch_);
    return {
        std::sin(yaw_) * horizontal,
        std::sin(pitch_),
        std::cos(yaw_) * horizontal
    };
}

}

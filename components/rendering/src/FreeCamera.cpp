#include <contract/rendering/FreeCamera.hpp>

#include <algorithm>
#include <cmath>

namespace contract::rendering {
namespace {

constexpr float kMaximumPitch = 1.553343F;
constexpr float kLookSpeed = 1.5F;

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

void FreeCamera::update(
    const FreeCameraInput& input,
    float elapsed_seconds) noexcept {
    if (!std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0.0F) {
        return;
    }
    const auto step = std::min(elapsed_seconds, 0.25F);
    yaw_ += std::clamp(input.yaw, -1.0F, 1.0F) *
        kLookSpeed * step;
    pitch_ = std::clamp(
        pitch_ +
            std::clamp(input.pitch, -1.0F, 1.0F) *
                kLookSpeed * step,
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

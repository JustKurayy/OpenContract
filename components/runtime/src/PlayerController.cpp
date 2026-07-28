#include <contract/runtime/PlayerController.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace contract::runtime {

PlayerController::PlayerController(
    scene::EntityId player,
    float movement_speed,
    float sprint_multiplier)
    : player_(std::move(player)),
      movement_speed_(movement_speed),
      sprint_multiplier_(sprint_multiplier) {}

core::Result<
    std::optional<RuntimeCommand>,
    PlayerControllerError>
PlayerController::update(
    const RuntimeObservation& observation,
    const PlayerInput& input,
    float elapsed_seconds) const {
    if (!std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0.0F) {
        return core::Result<
            std::optional<RuntimeCommand>,
            PlayerControllerError>::failure(
            {
                PlayerControllerErrorCode::invalid_elapsed_time,
                "Player movement elapsed time must be positive and finite"
            });
    }
    if (!std::isfinite(input.forward) ||
        !std::isfinite(input.right)) {
        return core::Result<
            std::optional<RuntimeCommand>,
            PlayerControllerError>::failure(
            {
                PlayerControllerErrorCode::invalid_input,
                "Player movement input must be finite"
            });
    }
    if (!std::isfinite(movement_speed_) ||
        !std::isfinite(sprint_multiplier_) ||
        movement_speed_ <= 0.0F ||
        sprint_multiplier_ < 1.0F) {
        return core::Result<
            std::optional<RuntimeCommand>,
            PlayerControllerError>::failure(
            {
                PlayerControllerErrorCode::invalid_configuration,
                "Player movement configuration is invalid"
            });
    }
    const auto player = std::find_if(
        observation.entities.begin(),
        observation.entities.end(),
        [this](const RuntimeEntityObservation& entity) {
            return entity.id == player_ && entity.enabled;
        });
    if (player == observation.entities.end()) {
        return core::Result<
            std::optional<RuntimeCommand>,
            PlayerControllerError>::failure(
            {
                PlayerControllerErrorCode::player_not_found,
                "Controlled player entity is unavailable: " +
                    player_.value()
            });
    }

    auto right = std::clamp(input.right, -1.0F, 1.0F);
    auto forward = std::clamp(input.forward, -1.0F, 1.0F);
    const auto length = std::sqrt(
        right * right + forward * forward);
    if (length <= 0.0001F) {
        return core::Result<
            std::optional<RuntimeCommand>,
            PlayerControllerError>::success(std::nullopt);
    }
    if (length > 1.0F) {
        right /= length;
        forward /= length;
    }

    auto transform = player->transform;
    const auto speed =
        movement_speed_ *
        (input.sprint ? sprint_multiplier_ : 1.0F);
    transform.position[0] += right * speed * elapsed_seconds;
    transform.position[2] += forward * speed * elapsed_seconds;
    const auto yaw = std::atan2(right, forward);
    transform.rotation = {
        0.0F,
        std::sin(yaw * 0.5F),
        0.0F,
        std::cos(yaw * 0.5F)
    };
    return core::Result<
        std::optional<RuntimeCommand>,
        PlayerControllerError>::success(
        RuntimeCommand{
            SetEntityTransformCommand{player_, transform}});
}

}

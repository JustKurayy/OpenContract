#pragma once

#include <contract/core/Result.hpp>
#include <contract/runtime/RuntimeCommand.hpp>
#include <contract/runtime/RuntimeObservation.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace contract::runtime {

inline constexpr std::string_view player_component_type{
    "contract.player"};

struct PlayerInput {
    float forward{0.0F};
    float right{0.0F};
    bool sprint{false};
};

enum class PlayerControllerErrorCode {
    invalid_elapsed_time,
    invalid_input,
    invalid_configuration,
    player_not_found
};

struct PlayerControllerError {
    PlayerControllerErrorCode code{
        PlayerControllerErrorCode::player_not_found};
    std::string message;
};

class PlayerController {
public:
    explicit PlayerController(
        scene::EntityId player,
        float movement_speed = 300.0F,
        float sprint_multiplier = 2.0F);

    [[nodiscard]] core::Result<
        std::optional<RuntimeCommand>,
        PlayerControllerError>
    update(
        const RuntimeObservation& observation,
        const PlayerInput& input,
        float elapsed_seconds) const;

private:
    scene::EntityId player_;
    float movement_speed_{300.0F};
    float sprint_multiplier_{2.0F};
};

}

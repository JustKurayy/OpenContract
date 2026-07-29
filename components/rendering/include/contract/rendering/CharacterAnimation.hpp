#pragma once

#include <contract/core/Result.hpp>
#include <contract/scene/RenderScene.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace contract::rendering {

enum class CharacterAnimationSequence {
    idle,
    walk,
    sprint
};

struct CharacterAnimationInput {
    float forward{0.0F};
    float right{0.0F};
    bool sprint{false};
};

struct CharacterAnimationState {
    CharacterAnimationSequence sequence{
        CharacterAnimationSequence::idle};
    float phase{0.0F};
};

struct CharacterAnimationTiming {
    float idle_duration_seconds{2.857142857F};
    float walk_duration_seconds{0.555555556F};
    float sprint_duration_seconds{0.357142857F};
};

enum class CharacterAnimationErrorCode {
    invalid_input,
    invalid_elapsed_time,
    invalid_configuration,
    invalid_model,
    invalid_state
};

struct CharacterAnimationError {
    CharacterAnimationErrorCode code{
        CharacterAnimationErrorCode::invalid_model};
    std::string message;
};

class CharacterAnimator {
public:
    [[nodiscard]] core::Result<void, CharacterAnimationError> configure(
        const CharacterAnimationTiming& timing);

    [[nodiscard]] core::Result<void, CharacterAnimationError> update(
        const CharacterAnimationInput& input,
        float elapsed_seconds);

    [[nodiscard]] const CharacterAnimationState& state() const noexcept;

private:
    CharacterAnimationState state_;
    CharacterAnimationTiming timing_;
};

[[nodiscard]] core::Result<
    std::vector<scene::RenderVertex>,
    CharacterAnimationError>
animate_character(
    const std::vector<scene::RenderVertex>& vertices,
    const CharacterAnimationState& state);

[[nodiscard]] core::Result<
    std::vector<scene::RenderVertex>,
    CharacterAnimationError>
animate_character(
    const std::vector<scene::RenderVertex>& vertices,
    const std::vector<scene::RenderSkinning>& skinning,
    const scene::RenderSkeleton& skeleton,
    const CharacterAnimationState& state);

[[nodiscard]] std::string_view character_sequence_name(
    CharacterAnimationSequence sequence) noexcept;

}

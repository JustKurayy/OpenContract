#include <contract/rendering/CharacterAnimation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace contract::rendering {
namespace {

constexpr float pi = 3.14159265358979323846F;

core::Result<void, CharacterAnimationError> failure(
    CharacterAnimationErrorCode code,
    std::string message) {
    return core::Result<void, CharacterAnimationError>::failure(
        {code, std::move(message)});
}

template <typename T>
core::Result<T, CharacterAnimationError> value_failure(
    CharacterAnimationErrorCode code,
    std::string message) {
    return core::Result<T, CharacterAnimationError>::failure(
        {code, std::move(message)});
}

CharacterAnimationSequence select_sequence(
    const CharacterAnimationInput& input) {
    const auto movement_squared =
        input.forward * input.forward +
        input.right * input.right;
    if (movement_squared <= 0.0001F) {
        return CharacterAnimationSequence::idle;
    }
    return input.sprint
        ? CharacterAnimationSequence::sprint
        : CharacterAnimationSequence::walk;
}

float cycles_per_second(CharacterAnimationSequence sequence) {
    switch (sequence) {
    case CharacterAnimationSequence::idle:
        return 0.35F;
    case CharacterAnimationSequence::walk:
        return 1.8F;
    case CharacterAnimationSequence::sprint:
        return 2.8F;
    }
    return 0.0F;
}

void rotate_about_x(
    scene::RenderVertex& vertex,
    float pivot_y,
    float pivot_z,
    float angle) {
    const auto relative_y = vertex.y - pivot_y;
    const auto relative_z = vertex.z - pivot_z;
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    vertex.y =
        pivot_y + relative_y * cosine - relative_z * sine;
    vertex.z =
        pivot_z + relative_y * sine + relative_z * cosine;
}

}

core::Result<void, CharacterAnimationError>
CharacterAnimator::update(
    const CharacterAnimationInput& input,
    float elapsed_seconds) {
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0F) {
        return failure(
            CharacterAnimationErrorCode::invalid_elapsed_time,
            "Character animation elapsed time must be finite and non-negative");
    }
    if (!std::isfinite(input.forward) ||
        !std::isfinite(input.right)) {
        return failure(
            CharacterAnimationErrorCode::invalid_input,
            "Character animation input must be finite");
    }
    const auto sequence = select_sequence(input);
    if (sequence != state_.sequence) {
        state_.sequence = sequence;
        state_.phase = 0.0F;
    }
    state_.phase = std::fmod(
        state_.phase +
            elapsed_seconds * cycles_per_second(sequence),
        1.0F);
    return core::Result<void, CharacterAnimationError>::success();
}

const CharacterAnimationState& CharacterAnimator::state() const noexcept {
    return state_;
}

core::Result<
    std::vector<scene::RenderVertex>,
    CharacterAnimationError>
animate_character(
    const std::vector<scene::RenderVertex>& vertices,
    const CharacterAnimationState& state) {
    if (vertices.empty()) {
        return value_failure<std::vector<scene::RenderVertex>>(
            CharacterAnimationErrorCode::invalid_model,
            "Character animation requires at least one vertex");
    }
    if (!std::isfinite(state.phase) ||
        state.phase < 0.0F ||
        state.phase >= 1.0F) {
        return value_failure<std::vector<scene::RenderVertex>>(
            CharacterAnimationErrorCode::invalid_state,
            "Character animation phase must be in the range [0, 1)");
    }

    auto minimum_x = std::numeric_limits<float>::max();
    auto minimum_y = std::numeric_limits<float>::max();
    auto minimum_z = std::numeric_limits<float>::max();
    auto maximum_x = std::numeric_limits<float>::lowest();
    auto maximum_y = std::numeric_limits<float>::lowest();
    auto maximum_z = std::numeric_limits<float>::lowest();
    for (const auto& vertex : vertices) {
        if (!std::isfinite(vertex.x) ||
            !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z) ||
            !std::isfinite(vertex.u) ||
            !std::isfinite(vertex.v)) {
            return value_failure<std::vector<scene::RenderVertex>>(
                CharacterAnimationErrorCode::invalid_model,
                "Character animation model contains a non-finite vertex");
        }
        minimum_x = (std::min)(minimum_x, vertex.x);
        minimum_y = (std::min)(minimum_y, vertex.y);
        minimum_z = (std::min)(minimum_z, vertex.z);
        maximum_x = (std::max)(maximum_x, vertex.x);
        maximum_y = (std::max)(maximum_y, vertex.y);
        maximum_z = (std::max)(maximum_z, vertex.z);
    }
    const auto height = maximum_y - minimum_y;
    const auto width = maximum_x - minimum_x;
    const auto depth = maximum_z - minimum_z;
    if (!std::isfinite(height) ||
        !std::isfinite(width) ||
        height <= 0.0001F ||
        width <= 0.0001F) {
        return value_failure<std::vector<scene::RenderVertex>>(
            CharacterAnimationErrorCode::invalid_model,
            "Character animation model must have non-zero width and height");
    }

    const auto center_z = (minimum_z + maximum_z) * 0.5F;
    const auto hip_y = minimum_y + height * 0.5F;
    const auto stride = std::sin(state.phase * 2.0F * pi);
    const auto body_bob =
        state.sequence == CharacterAnimationSequence::idle
            ? std::sin(state.phase * 2.0F * pi) * height * 0.002F
            : std::abs(stride) * height *
                  (state.sequence ==
                           CharacterAnimationSequence::sprint
                       ? 0.012F
                       : 0.006F);
    const auto lean =
        state.sequence == CharacterAnimationSequence::sprint
            ? -0.09F + stride * 0.025F
            : state.sequence == CharacterAnimationSequence::walk
                ? stride * 0.018F
                : 0.0F;
    const auto lateral_sway =
        state.sequence == CharacterAnimationSequence::idle
            ? 0.0F
            : stride * width *
                  (state.sequence ==
                           CharacterAnimationSequence::sprint
                       ? 0.006F
                       : 0.003F);

    auto posed = vertices;
    for (auto& vertex : posed) {
        if (lean != 0.0F) {
            rotate_about_x(
                vertex,
                hip_y,
                center_z,
                lean);
        }
        vertex.x += lateral_sway;
        vertex.y += body_bob;
        if (state.sequence ==
            CharacterAnimationSequence::idle) {
            vertex.z +=
                std::sin(state.phase * 2.0F * pi) *
                (std::max)(depth, height * 0.1F) *
                0.003F;
        }
    }
    return core::Result<
        std::vector<scene::RenderVertex>,
        CharacterAnimationError>::success(std::move(posed));
}

std::string_view character_sequence_name(
    CharacterAnimationSequence sequence) noexcept {
    switch (sequence) {
    case CharacterAnimationSequence::idle:
        return "idle";
    case CharacterAnimationSequence::walk:
        return "walk";
    case CharacterAnimationSequence::sprint:
        return "sprint";
    }
    return "unknown";
}

}

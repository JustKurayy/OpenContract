#include <contract/rendering/CharacterAnimation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
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

float duration_seconds(
    const CharacterAnimationTiming& timing,
    CharacterAnimationSequence sequence) {
    switch (sequence) {
    case CharacterAnimationSequence::idle:
        return timing.idle_duration_seconds;
    case CharacterAnimationSequence::walk:
        return timing.walk_duration_seconds;
    case CharacterAnimationSequence::sprint:
        return timing.sprint_duration_seconds;
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

struct AffinePose {
    std::array<float, 9> rotation{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F
    };
    std::array<float, 3> translation{0.0F, 0.0F, 0.0F};
};

std::array<float, 3> transform_position(
    const AffinePose& pose,
    const std::array<float, 3>& position) {
    return {
        pose.rotation[0] * position[0] +
            pose.rotation[1] * position[1] +
            pose.rotation[2] * position[2] +
            pose.translation[0],
        pose.rotation[3] * position[0] +
            pose.rotation[4] * position[1] +
            pose.rotation[5] * position[2] +
            pose.translation[1],
        pose.rotation[6] * position[0] +
            pose.rotation[7] * position[1] +
            pose.rotation[8] * position[2] +
            pose.translation[2]
    };
}

AffinePose compose(
    const AffinePose& after,
    const AffinePose& before) {
    AffinePose result;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 3; ++inner) {
                value +=
                    after.rotation[row * 3U + inner] *
                    before.rotation[inner * 3U + column];
            }
            result.rotation[row * 3U + column] = value;
        }
    }
    const auto translated =
        transform_position(after, before.translation);
    result.translation = translated;
    return result;
}

AffinePose rotation_about_x(float angle) {
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    AffinePose result;
    result.rotation = {
        1.0F, 0.0F, 0.0F,
        0.0F, cosine, -sine,
        0.0F, sine, cosine
    };
    return result;
}

std::optional<AffinePose> inverse(const AffinePose& pose) {
    const auto& matrix = pose.rotation;
    const auto determinant =
        matrix[0] *
            (matrix[4] * matrix[8] -
             matrix[5] * matrix[7]) -
        matrix[1] *
            (matrix[3] * matrix[8] -
             matrix[5] * matrix[6]) +
        matrix[2] *
            (matrix[3] * matrix[7] -
             matrix[4] * matrix[6]);
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= 0.00000001F) {
        return std::nullopt;
    }

    const auto reciprocal = 1.0F / determinant;
    AffinePose result;
    result.rotation = {
        (matrix[4] * matrix[8] -
         matrix[5] * matrix[7]) * reciprocal,
        (matrix[2] * matrix[7] -
         matrix[1] * matrix[8]) * reciprocal,
        (matrix[1] * matrix[5] -
         matrix[2] * matrix[4]) * reciprocal,
        (matrix[5] * matrix[6] -
         matrix[3] * matrix[8]) * reciprocal,
        (matrix[0] * matrix[8] -
         matrix[2] * matrix[6]) * reciprocal,
        (matrix[2] * matrix[3] -
         matrix[0] * matrix[5]) * reciprocal,
        (matrix[3] * matrix[7] -
         matrix[4] * matrix[6]) * reciprocal,
        (matrix[1] * matrix[6] -
         matrix[0] * matrix[7]) * reciprocal,
        (matrix[0] * matrix[4] -
         matrix[1] * matrix[3]) * reciprocal
    };
    const auto translated = transform_position(
        result,
        {
            -pose.translation[0],
            -pose.translation[1],
            -pose.translation[2]
        });
    result.translation = translated;
    return result;
}

bool finite_pose(const AffinePose& pose) {
    return std::all_of(
               pose.rotation.begin(),
               pose.rotation.end(),
               [](float value) {
                   return std::isfinite(value);
               }) &&
           std::all_of(
               pose.translation.begin(),
               pose.translation.end(),
               [](float value) {
                   return std::isfinite(value);
               });
}

float joint_angle(
    scene::RenderJointRole role,
    const CharacterAnimationState& state) {
    const auto stride =
        std::sin(state.phase * 2.0F * pi);
    if (state.sequence == CharacterAnimationSequence::idle) {
        switch (role) {
        case scene::RenderJointRole::spine_upper:
            return stride * 0.012F;
        case scene::RenderJointRole::neck:
            return stride * -0.006F;
        default:
            return 0.0F;
        }
    }

    const auto sprint =
        state.sequence == CharacterAnimationSequence::sprint;
    const auto leg_swing = sprint ? 0.78F : 0.48F;
    const auto arm_swing = sprint ? 0.65F : 0.38F;
    const auto knee_bend = sprint ? 0.72F : 0.48F;
    switch (role) {
    case scene::RenderJointRole::left_thigh:
        return stride * leg_swing;
    case scene::RenderJointRole::right_thigh:
        return -stride * leg_swing;
    case scene::RenderJointRole::left_calf:
        return std::max(0.0F, -stride) * knee_bend;
    case scene::RenderJointRole::right_calf:
        return std::max(0.0F, stride) * knee_bend;
    case scene::RenderJointRole::left_upper_arm:
        return -stride * arm_swing;
    case scene::RenderJointRole::right_upper_arm:
        return stride * arm_swing;
    case scene::RenderJointRole::left_forearm:
    case scene::RenderJointRole::right_forearm:
        return std::abs(stride) * -0.18F;
    case scene::RenderJointRole::spine_lower:
        return sprint ? -0.11F : -0.025F;
    case scene::RenderJointRole::spine_upper:
        return stride * (sprint ? 0.035F : 0.02F);
    default:
        return 0.0F;
    }
}

}

core::Result<void, CharacterAnimationError>
CharacterAnimator::configure(
    const CharacterAnimationTiming& timing) {
    if (!std::isfinite(timing.idle_duration_seconds) ||
        !std::isfinite(timing.walk_duration_seconds) ||
        !std::isfinite(timing.sprint_duration_seconds) ||
        timing.idle_duration_seconds <= 0.0F ||
        timing.walk_duration_seconds <= 0.0F ||
        timing.sprint_duration_seconds <= 0.0F) {
        return failure(
            CharacterAnimationErrorCode::invalid_configuration,
            "Character animation durations must be positive and finite");
    }
    timing_ = timing;
    state_.phase = 0.0F;
    return core::Result<void, CharacterAnimationError>::success();
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
            elapsed_seconds /
                duration_seconds(timing_, sequence),
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

core::Result<
    std::vector<scene::RenderVertex>,
    CharacterAnimationError>
animate_character(
    const std::vector<scene::RenderVertex>& vertices,
    const std::vector<scene::RenderSkinning>& skinning,
    const scene::RenderSkeleton& skeleton,
    const CharacterAnimationState& state) {
    if (!std::isfinite(state.phase) ||
        state.phase < 0.0F ||
        state.phase >= 1.0F) {
        return value_failure<std::vector<scene::RenderVertex>>(
            CharacterAnimationErrorCode::invalid_state,
            "Character animation phase must be in the range [0, 1)");
    }

    CharacterPose pose;
    pose.joint_deltas.reserve(skeleton.joints.size());
    for (const auto& joint : skeleton.joints) {
        const auto delta = rotation_about_x(
            joint_angle(joint.role, state));
        pose.joint_deltas.push_back(
            {
                delta.rotation,
                delta.translation
            });
    }
    return animate_character(
        vertices,
        skinning,
        skeleton,
        pose);
}

core::Result<
    std::vector<scene::RenderVertex>,
    CharacterAnimationError>
animate_character(
    const std::vector<scene::RenderVertex>& vertices,
    const std::vector<scene::RenderSkinning>& skinning,
    const scene::RenderSkeleton& skeleton,
    const CharacterPose& pose) {
    if (vertices.empty() ||
        skinning.size() != vertices.size() ||
        skeleton.joints.empty()) {
        return value_failure<std::vector<scene::RenderVertex>>(
            CharacterAnimationErrorCode::invalid_model,
            "Skeletal character data is empty or misaligned");
    }
    if (pose.joint_deltas.size() != skeleton.joints.size()) {
        return value_failure<std::vector<scene::RenderVertex>>(
            CharacterAnimationErrorCode::invalid_state,
            "Character pose must contain one delta per skeleton joint");
    }

    std::vector<AffinePose> inverse_bind_poses;
    std::vector<AffinePose> posed_joints;
    std::vector<AffinePose> skinning_poses;
    inverse_bind_poses.reserve(skeleton.joints.size());
    posed_joints.reserve(skeleton.joints.size());
    skinning_poses.reserve(skeleton.joints.size());
    for (std::size_t index = 0;
         index < skeleton.joints.size();
         ++index) {
        const auto& joint = skeleton.joints[index];
        const auto& delta = pose.joint_deltas[index];
        const AffinePose bind{
            joint.reference_basis,
            joint.reference_position
        };
        const AffinePose delta_pose{
            delta.rotation,
            delta.translation
        };
        if (!finite_pose(bind) ||
            !finite_pose(delta_pose) ||
            (joint.parent_index.has_value() &&
             joint.parent_index.value() >= index)) {
            return value_failure<std::vector<scene::RenderVertex>>(
                CharacterAnimationErrorCode::invalid_model,
                "Character skeleton or pose is non-finite or not topological");
        }
        const auto inverse_bind = inverse(bind);
        if (!inverse_bind.has_value()) {
            return value_failure<std::vector<scene::RenderVertex>>(
                CharacterAnimationErrorCode::invalid_model,
                "Character reference basis is not invertible");
        }

        AffinePose posed;
        if (joint.parent_index.has_value()) {
            const auto parent = joint.parent_index.value();
            const auto local_bind = compose(
                inverse_bind_poses[parent],
                bind);
            const auto local_pose = compose(
                local_bind,
                delta_pose);
            posed = compose(
                posed_joints[parent],
                local_pose);
        } else {
            posed = compose(bind, delta_pose);
        }
        inverse_bind_poses.push_back(
            inverse_bind.value());
        posed_joints.push_back(posed);
        skinning_poses.push_back(
            compose(posed, inverse_bind.value()));
    }

    auto posed = vertices;
    for (std::size_t vertex_index = 0;
         vertex_index < vertices.size();
         ++vertex_index) {
        const auto& vertex = vertices[vertex_index];
        if (!std::isfinite(vertex.x) ||
            !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z) ||
            !std::isfinite(vertex.u) ||
            !std::isfinite(vertex.v)) {
            return value_failure<std::vector<scene::RenderVertex>>(
                CharacterAnimationErrorCode::invalid_model,
                "Skeletal character contains a non-finite vertex");
        }

        std::array<float, 3> accumulated{0.0F, 0.0F, 0.0F};
        float total_weight = 0.0F;
        for (std::size_t influence = 0;
             influence < skinning[vertex_index].weights.size();
             ++influence) {
            const auto weight =
                skinning[vertex_index].weights[influence];
            const auto joint =
                skinning[vertex_index].joints[influence];
            if (!std::isfinite(weight) ||
                weight < 0.0F ||
                weight > 1.0F ||
                (weight > 0.0F &&
                 joint >= skinning_poses.size())) {
                return value_failure<
                    std::vector<scene::RenderVertex>>(
                    CharacterAnimationErrorCode::invalid_model,
                    "Character skinning contains an invalid influence");
            }
            if (weight == 0.0F) {
                continue;
            }
            const auto transformed = transform_position(
                skinning_poses[joint],
                {vertex.x, vertex.y, vertex.z});
            for (std::size_t axis = 0; axis < 3; ++axis) {
                accumulated[axis] +=
                    transformed[axis] * weight;
            }
            total_weight += weight;
        }
        if (!std::isfinite(total_weight) ||
            std::abs(total_weight - 1.0F) > 0.001F) {
            return value_failure<std::vector<scene::RenderVertex>>(
                CharacterAnimationErrorCode::invalid_model,
                "Character skinning weights are not normalized");
        }
        posed[vertex_index].x = accumulated[0];
        posed[vertex_index].y = accumulated[1];
        posed[vertex_index].z = accumulated[2];
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

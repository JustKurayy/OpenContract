#include "TestSupport.hpp"

#include <contract/rendering/CharacterAnimation.hpp>

#include <cmath>
#include <limits>

namespace {

contract::scene::RenderScene character_scene() {
    contract::scene::RenderScene scene;
    scene.vertices = {
        {-20.0F, 0.0F, 0.0F, 0.0F, 0.0F},
        {20.0F, 0.0F, 0.0F, 1.0F, 0.0F},
        {-25.0F, 80.0F, 0.0F, 0.0F, 0.5F},
        {25.0F, 80.0F, 0.0F, 1.0F, 0.5F},
        {-50.0F, 145.0F, 0.0F, 0.0F, 0.75F},
        {50.0F, 145.0F, 0.0F, 1.0F, 0.75F},
        {-10.0F, 180.0F, 0.0F, 0.0F, 1.0F},
        {10.0F, 180.0F, 0.0F, 1.0F, 1.0F}
    };
    return scene;
}

bool differs(
    const contract::scene::RenderVertex& left,
    const contract::scene::RenderVertex& right) {
    return std::abs(left.x - right.x) > 0.0001F ||
           std::abs(left.y - right.y) > 0.0001F ||
           std::abs(left.z - right.z) > 0.0001F;
}

}

int main() {
    using contract::rendering::CharacterAnimationInput;
    using contract::rendering::CharacterAnimationSequence;
    using contract::rendering::CharacterAnimator;

    CharacterAnimator animator;
    CONTRACT_EXPECT_EQ(
        animator.state().sequence,
        CharacterAnimationSequence::idle);
    CONTRACT_EXPECT_EQ(animator.state().phase, 0.0F);

    auto idle = animator.update({}, 0.25F);
    CONTRACT_EXPECT(idle.has_value());
    CONTRACT_EXPECT_EQ(
        animator.state().sequence,
        CharacterAnimationSequence::idle);

    auto walk = animator.update(
        CharacterAnimationInput{1.0F, 0.0F, false},
        0.25F);
    CONTRACT_EXPECT(walk.has_value());
    CONTRACT_EXPECT_EQ(
        animator.state().sequence,
        CharacterAnimationSequence::walk);
    const auto walk_phase = animator.state().phase;
    CONTRACT_EXPECT(walk_phase > 0.0F);
    CONTRACT_EXPECT(walk_phase < 1.0F);

    auto sprint = animator.update(
        CharacterAnimationInput{1.0F, 0.0F, true},
        0.25F);
    CONTRACT_EXPECT(sprint.has_value());
    CONTRACT_EXPECT_EQ(
        animator.state().sequence,
        CharacterAnimationSequence::sprint);
    CONTRACT_EXPECT(animator.state().phase != walk_phase);

    auto stopped = animator.update({}, 0.1F);
    CONTRACT_EXPECT(stopped.has_value());
    CONTRACT_EXPECT_EQ(
        animator.state().sequence,
        CharacterAnimationSequence::idle);

    const auto scene = character_scene();
    animator = CharacterAnimator{};
    auto advanced = animator.update(
        CharacterAnimationInput{1.0F, 0.0F, false},
        0.125F);
    CONTRACT_EXPECT(advanced.has_value());
    auto posed = contract::rendering::animate_character(
        scene.vertices,
        animator.state());
    CONTRACT_EXPECT(posed.has_value());
    CONTRACT_EXPECT_EQ(posed.value().size(), scene.vertices.size());
    CONTRACT_EXPECT(
        differs(posed.value()[0], scene.vertices[0]));
    CONTRACT_EXPECT(
        differs(posed.value()[1], scene.vertices[1]));
    CONTRACT_EXPECT(
        differs(posed.value()[4], scene.vertices[4]));
    CONTRACT_EXPECT(
        differs(posed.value()[5], scene.vertices[5]));
    CONTRACT_EXPECT_EQ(posed.value()[0].u, scene.vertices[0].u);
    CONTRACT_EXPECT_EQ(posed.value()[0].v, scene.vertices[0].v);

    auto invalid_time = animator.update(
        {},
        std::numeric_limits<float>::infinity());
    CONTRACT_EXPECT(!invalid_time.has_value());
    auto negative_time = animator.update({}, -0.01F);
    CONTRACT_EXPECT(!negative_time.has_value());
    auto invalid_input = animator.update(
        CharacterAnimationInput{
            std::numeric_limits<float>::quiet_NaN(),
            0.0F,
            false},
        0.01F);
    CONTRACT_EXPECT(!invalid_input.has_value());

    auto empty_pose = contract::rendering::animate_character(
        {},
        animator.state());
    CONTRACT_EXPECT(!empty_pose.has_value());

    auto invalid_vertices = scene.vertices;
    invalid_vertices[0].y =
        std::numeric_limits<float>::quiet_NaN();
    auto invalid_pose = contract::rendering::animate_character(
        invalid_vertices,
        animator.state());
    CONTRACT_EXPECT(!invalid_pose.has_value());

    return contract::test::finish();
}

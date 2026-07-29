#include "TestSupport.hpp"

#include <contract/collision/StaticCollisionWorld.hpp>
#include <contract/scene/CollisionScene.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

bool near(float left, float right) {
    return std::fabs(left - right) < 0.001F;
}

void append_floor(
    contract::scene::CollisionScene& scene,
    float minimum_x,
    float maximum_x,
    float minimum_z,
    float maximum_z,
    float height) {
    const auto base =
        static_cast<std::uint32_t>(scene.vertices.size());
    scene.vertices.insert(
        scene.vertices.end(),
        {
            {minimum_x, height, minimum_z},
            {maximum_x, height, minimum_z},
            {maximum_x, height, maximum_z},
            {minimum_x, height, maximum_z}
        });
    scene.indices.insert(
        scene.indices.end(),
        {
            base,
            base + 2U,
            base + 1U,
            base,
            base + 3U,
            base + 2U
        });
}

void append_wall(
    contract::scene::CollisionScene& scene,
    float x,
    float minimum_y,
    float maximum_y,
    float minimum_z,
    float maximum_z) {
    const auto base =
        static_cast<std::uint32_t>(scene.vertices.size());
    scene.vertices.insert(
        scene.vertices.end(),
        {
            {x, minimum_y, minimum_z},
            {x, maximum_y, minimum_z},
            {x, maximum_y, maximum_z},
            {x, minimum_y, maximum_z}
        });
    scene.indices.insert(
        scene.indices.end(),
        {
            base,
            base + 1U,
            base + 2U,
            base,
            base + 2U,
            base + 3U
        });
}

}

int main() {
    using namespace contract;

    scene::CollisionScene geometry;
    append_floor(geometry, -100.0F, 100.0F, -100.0F, 100.0F, 0.0F);
    append_wall(geometry, 10.0F, 0.0F, 200.0F, -20.0F, 20.0F);
    auto world = collision::StaticCollisionWorld::create(
        std::move(geometry));
    CONTRACT_EXPECT(world.has_value());
    if (world.has_value()) {
        CONTRACT_EXPECT_EQ(world.value().triangle_count(), std::size_t{4});
        const auto ground = world.value().find_ground(
            {
                0.0F,
                0.0F,
                10.0F,
                25.0F,
                50.0F
            });
        CONTRACT_EXPECT(ground.has_value());
        CONTRACT_EXPECT(near(ground->height, 0.0F));

        const collision::GroundedMotionConfig config{
            2.0F,
            180.0F,
            25.0F,
            50.0F
        };
        const auto moved = world.value().resolve_grounded_motion(
            {0.0F, 0.0F, 0.0F},
            {5.0F, 0.0F, 0.0F},
            config);
        CONTRACT_EXPECT(moved.has_value());
        CONTRACT_EXPECT(!moved.value().blocked);
        CONTRACT_EXPECT(moved.value().grounded);
        CONTRACT_EXPECT(near(moved.value().position[0], 5.0F));
        CONTRACT_EXPECT(near(moved.value().position[1], 0.0F));

        const auto wall_blocked =
            world.value().resolve_grounded_motion(
                {0.0F, 0.0F, 0.0F},
                {20.0F, 0.0F, 0.0F},
                config);
        CONTRACT_EXPECT(wall_blocked.has_value());
        CONTRACT_EXPECT(wall_blocked.value().blocked);
        CONTRACT_EXPECT(near(
            wall_blocked.value().position[0],
            0.0F));

        const auto wall_slid =
            world.value().resolve_grounded_motion(
                {0.0F, 0.0F, 0.0F},
                {20.0F, 0.0F, 8.0F},
                config);
        CONTRACT_EXPECT(wall_slid.has_value());
        CONTRACT_EXPECT(wall_slid.value().blocked);
        CONTRACT_EXPECT(wall_slid.value().grounded);
        CONTRACT_EXPECT(near(
            wall_slid.value().position[0],
            0.0F));
        CONTRACT_EXPECT(near(
            wall_slid.value().position[2],
            8.0F));

        const auto no_ground =
            world.value().resolve_grounded_motion(
                {0.0F, 0.0F, 0.0F},
                {200.0F, 0.0F, 0.0F},
                config);
        CONTRACT_EXPECT(no_ground.has_value());
        CONTRACT_EXPECT(no_ground.value().blocked);
    }

    scene::CollisionScene step_geometry;
    append_floor(
        step_geometry,
        -20.0F,
        0.0F,
        -20.0F,
        20.0F,
        0.0F);
    append_floor(
        step_geometry,
        0.0F,
        20.0F,
        -20.0F,
        20.0F,
        20.0F);
    auto step_world = collision::StaticCollisionWorld::create(
        std::move(step_geometry));
    CONTRACT_EXPECT(step_world.has_value());
    const collision::GroundedMotionConfig step_config{
        10.0F,
        180.0F,
        25.0F,
        50.0F
    };
    const auto stepped = step_world.value().resolve_grounded_motion(
        {-5.0F, 0.0F, 0.0F},
        {5.0F, 0.0F, 0.0F},
        step_config);
    CONTRACT_EXPECT(stepped.has_value());
    CONTRACT_EXPECT(!stepped.value().blocked);
    CONTRACT_EXPECT(near(stepped.value().position[1], 20.0F));

    scene::CollisionScene drop_geometry;
    append_floor(
        drop_geometry,
        -20.0F,
        0.0F,
        -20.0F,
        20.0F,
        0.0F);
    append_floor(
        drop_geometry,
        0.0F,
        20.0F,
        -20.0F,
        20.0F,
        -200.0F);
    auto drop_world = collision::StaticCollisionWorld::create(
        std::move(drop_geometry));
    CONTRACT_EXPECT(drop_world.has_value());
    const auto dropped = drop_world.value().resolve_grounded_motion(
        {-5.0F, 0.0F, 0.0F},
        {5.0F, 0.0F, 0.0F},
        step_config);
    CONTRACT_EXPECT(dropped.has_value());
    CONTRACT_EXPECT(dropped.value().blocked);

    scene::CollisionScene malformed;
    malformed.vertices = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {
            0.0F,
            std::numeric_limits<float>::quiet_NaN(),
            1.0F
        }
    };
    malformed.indices = {0, 1, 2};
    const auto rejected =
        collision::StaticCollisionWorld::create(
            std::move(malformed));
    CONTRACT_EXPECT(!rejected.has_value());
    CONTRACT_EXPECT_EQ(
        rejected.error().code,
        collision::StaticCollisionErrorCode::invalid_vertex);

    return test::finish();
}

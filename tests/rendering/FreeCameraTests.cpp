#include "TestSupport.hpp"

#include <contract/rendering/FreeCamera.hpp>

#include <cmath>

namespace {

bool near(float left, float right) {
    return std::fabs(left - right) < 0.001F;
}

}

int main() {
    contract::rendering::FreeCamera camera;
    camera.frame_scene({10.0F, 20.0F, 30.0F}, 100.0F);

    const auto initial_target = camera.target();
    CONTRACT_EXPECT(near(initial_target.x, 10.0F));
    CONTRACT_EXPECT(near(initial_target.y, 20.0F));
    CONTRACT_EXPECT(near(initial_target.z, 30.0F));

    const auto initial_position = camera.position();
    contract::rendering::FreeCameraInput forward;
    forward.forward = 1.0F;
    camera.update(forward, 1.0F);
    const auto moved_position = camera.position();
    const auto initial_distance =
        std::sqrt(
            std::pow(initial_position.x - 10.0F, 2.0F) +
            std::pow(initial_position.y - 20.0F, 2.0F) +
            std::pow(initial_position.z - 30.0F, 2.0F));
    const auto moved_distance =
        std::sqrt(
            std::pow(moved_position.x - 10.0F, 2.0F) +
            std::pow(moved_position.y - 20.0F, 2.0F) +
            std::pow(moved_position.z - 30.0F, 2.0F));
    CONTRACT_EXPECT(moved_distance < initial_distance);

    contract::rendering::FreeCameraInput look;
    look.pitch = 100.0F;
    camera.update(look, 1.0F);
    CONTRACT_EXPECT(camera.pitch() < 1.5708F);

    contract::rendering::FreeCamera follow;
    follow.frame_subject({5.0F, 90.0F, 10.0F}, 500.0F);
    const auto followed_target = follow.target();
    CONTRACT_EXPECT(near(followed_target.x, 5.0F));
    CONTRACT_EXPECT(near(followed_target.y, 90.0F));
    CONTRACT_EXPECT(near(followed_target.z, 10.0F));

    contract::rendering::FreeCameraInput orbit;
    orbit.yaw = 1.0F;
    const auto before_orbit = follow.position();
    follow.orbit_subject(
        {15.0F, 90.0F, 20.0F},
        orbit,
        0.5F);
    const auto after_orbit = follow.position();
    const auto orbit_target = follow.target();
    CONTRACT_EXPECT(!near(before_orbit.x, after_orbit.x));
    CONTRACT_EXPECT(near(orbit_target.x, 15.0F));
    CONTRACT_EXPECT(near(orbit_target.y, 90.0F));
    CONTRACT_EXPECT(near(orbit_target.z, 20.0F));

    return contract::test::finish();
}

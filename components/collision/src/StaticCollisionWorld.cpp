#include <contract/collision/StaticCollisionWorld.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace contract::collision {
namespace {

constexpr float kEpsilon = 0.0001F;
constexpr float kMinimumWalkableNormal = 0.5F;

bool finite(std::array<float, 3> value) {
    return std::all_of(
        value.begin(),
        value.end(),
        [](float component) {
            return std::isfinite(component);
        });
}

std::array<float, 3> subtract(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) {
    return {
        left[0] - right[0],
        left[1] - right[1],
        left[2] - right[2]
    };
}

std::array<float, 3> cross(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) {
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]
    };
}

float dot(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) {
    return
        left[0] * right[0] +
        left[1] * right[1] +
        left[2] * right[2];
}

float length(const std::array<float, 3>& value) {
    return std::sqrt(dot(value, value));
}

template <typename Triangle>
bool point_height(
    const Triangle& triangle,
    float x,
    float z,
    float& height) {
    const auto denominator =
        (triangle.b[2] - triangle.c[2]) *
            (triangle.a[0] - triangle.c[0]) +
        (triangle.c[0] - triangle.b[0]) *
            (triangle.a[2] - triangle.c[2]);
    if (std::fabs(denominator) <= kEpsilon) {
        return false;
    }
    const auto weight_a =
        ((triangle.b[2] - triangle.c[2]) *
             (x - triangle.c[0]) +
         (triangle.c[0] - triangle.b[0]) *
             (z - triangle.c[2])) /
        denominator;
    const auto weight_b =
        ((triangle.c[2] - triangle.a[2]) *
             (x - triangle.c[0]) +
         (triangle.a[0] - triangle.c[0]) *
             (z - triangle.c[2])) /
        denominator;
    const auto weight_c = 1.0F - weight_a - weight_b;
    if (weight_a < -kEpsilon ||
        weight_b < -kEpsilon ||
        weight_c < -kEpsilon) {
        return false;
    }
    height =
        weight_a * triangle.a[1] +
        weight_b * triangle.b[1] +
        weight_c * triangle.c[1];
    return std::isfinite(height);
}

template <typename Triangle>
bool segment_intersects_triangle(
    const std::array<float, 3>& start,
    const std::array<float, 3>& end,
    const Triangle& triangle) {
    const auto direction = subtract(end, start);
    const auto edge_one = subtract(triangle.b, triangle.a);
    const auto edge_two = subtract(triangle.c, triangle.a);
    const auto p = cross(direction, edge_two);
    const auto determinant = dot(edge_one, p);
    if (std::fabs(determinant) <= kEpsilon) {
        return false;
    }
    const auto inverse = 1.0F / determinant;
    const auto from_a = subtract(start, triangle.a);
    const auto u = dot(from_a, p) * inverse;
    if (u < -kEpsilon || u > 1.0F + kEpsilon) {
        return false;
    }
    const auto q = cross(from_a, edge_one);
    const auto v = dot(direction, q) * inverse;
    if (v < -kEpsilon || u + v > 1.0F + kEpsilon) {
        return false;
    }
    const auto distance = dot(edge_two, q) * inverse;
    return distance >= -kEpsilon && distance <= 1.0F + kEpsilon;
}

}

StaticCollisionWorld::StaticCollisionWorld(
    std::vector<Triangle> triangles)
    : triangles_(std::move(triangles)) {}

core::Result<StaticCollisionWorld, StaticCollisionError>
StaticCollisionWorld::create(scene::CollisionScene scene) {
    for (const auto& vertex : scene.vertices) {
        if (!std::isfinite(vertex.x) ||
            !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z)) {
            return core::Result<
                StaticCollisionWorld,
                StaticCollisionError>::failure(
                {
                    StaticCollisionErrorCode::invalid_vertex,
                    "Static collision vertex must be finite"
                });
        }
    }
    if (scene.indices.size() % 3U != 0U) {
        return core::Result<
            StaticCollisionWorld,
            StaticCollisionError>::failure(
            {
                StaticCollisionErrorCode::invalid_topology,
                "Static collision indices must form triangles"
            });
    }

    std::vector<Triangle> triangles;
    triangles.reserve(scene.indices.size() / 3U);
    for (std::size_t index = 0;
         index < scene.indices.size();
         index += 3U) {
        const auto first = scene.indices[index];
        const auto second = scene.indices[index + 1U];
        const auto third = scene.indices[index + 2U];
        if (first >= scene.vertices.size() ||
            second >= scene.vertices.size() ||
            third >= scene.vertices.size()) {
            return core::Result<
                StaticCollisionWorld,
                StaticCollisionError>::failure(
                {
                    StaticCollisionErrorCode::index_out_of_range,
                    "Static collision index exceeds its vertex array"
                });
        }
        const auto& source_a = scene.vertices[first];
        const auto& source_b = scene.vertices[second];
        const auto& source_c = scene.vertices[third];
        Triangle triangle{
            {source_a.x, source_a.y, source_a.z},
            {source_b.x, source_b.y, source_b.z},
            {source_c.x, source_c.y, source_c.z},
            {}
        };
        triangle.normal = cross(
            subtract(triangle.b, triangle.a),
            subtract(triangle.c, triangle.a));
        if (length(triangle.normal) <= kEpsilon) {
            return core::Result<
                StaticCollisionWorld,
                StaticCollisionError>::failure(
                {
                    StaticCollisionErrorCode::degenerate_triangle,
                    "Static collision triangle is degenerate"
                });
        }
        triangles.push_back(triangle);
    }
    return core::Result<
        StaticCollisionWorld,
        StaticCollisionError>::success(
        StaticCollisionWorld(std::move(triangles)));
}

std::optional<GroundContact> StaticCollisionWorld::find_ground(
    const GroundQuery& query) const noexcept {
    if (!std::isfinite(query.x) ||
        !std::isfinite(query.z) ||
        !std::isfinite(query.reference_height) ||
        !std::isfinite(query.maximum_step_up) ||
        !std::isfinite(query.maximum_drop) ||
        query.maximum_step_up < 0.0F ||
        query.maximum_drop < 0.0F) {
        return std::nullopt;
    }
    std::optional<GroundContact> best;
    for (std::size_t index = 0;
         index < triangles_.size();
         ++index) {
        const auto& triangle = triangles_[index];
        const auto normal_length = length(triangle.normal);
        if (normal_length <= kEpsilon ||
            std::fabs(triangle.normal[1]) / normal_length <
                kMinimumWalkableNormal) {
            continue;
        }
        float height = 0.0F;
        if (!point_height(
                triangle,
                query.x,
                query.z,
                height) ||
            height >
                query.reference_height +
                    query.maximum_step_up +
                    kEpsilon ||
            height <
                query.reference_height -
                    query.maximum_drop -
                    kEpsilon) {
            continue;
        }
        if (!best.has_value() || height > best->height) {
            best = GroundContact{height, index};
        }
    }
    return best;
}

core::Result<GroundedMotion, StaticCollisionError>
StaticCollisionWorld::resolve_grounded_motion(
    std::array<float, 3> start,
    std::array<float, 3> desired,
    const GroundedMotionConfig& config) const {
    if (!finite(start) ||
        !finite(desired) ||
        !std::isfinite(config.radius) ||
        !std::isfinite(config.height) ||
        !std::isfinite(config.maximum_step_up) ||
        !std::isfinite(config.maximum_drop) ||
        config.radius < 0.0F ||
        config.height <= 0.0F ||
        config.maximum_step_up < 0.0F ||
        config.maximum_drop < 0.0F) {
        return core::Result<
            GroundedMotion,
            StaticCollisionError>::failure(
            {
                StaticCollisionErrorCode::invalid_configuration,
                "Grounded motion configuration is invalid"
            });
    }

    const auto ground = find_ground(
        {
            desired[0],
            desired[2],
            start[1],
            config.maximum_step_up,
            config.maximum_drop
        });
    if (!ground.has_value()) {
        return core::Result<
            GroundedMotion,
            StaticCollisionError>::success(
            GroundedMotion{start, false, true});
    }
    desired[1] = ground->height;

    const auto delta_x = desired[0] - start[0];
    const auto delta_z = desired[2] - start[2];
    const auto horizontal_length =
        std::sqrt(delta_x * delta_x + delta_z * delta_z);
    if (horizontal_length > kEpsilon) {
        const auto direction_x = delta_x / horizontal_length;
        const auto direction_z = delta_z / horizontal_length;
        const auto perpendicular_x = -direction_z;
        const auto perpendicular_z = direction_x;
        for (const auto& triangle : triangles_) {
            const auto normal_length = length(triangle.normal);
            if (normal_length <= kEpsilon ||
                std::fabs(triangle.normal[1]) / normal_length >
                    kMinimumWalkableNormal) {
                continue;
            }
            for (const float lateral : {
                     -config.radius,
                     0.0F,
                     config.radius}) {
                const std::array<float, 3> ray_start{
                    start[0] +
                        direction_x * config.radius +
                        perpendicular_x * lateral,
                    start[1] + config.height * 0.5F,
                    start[2] +
                        direction_z * config.radius +
                        perpendicular_z * lateral
                };
                const std::array<float, 3> ray_end{
                    desired[0] +
                        direction_x * config.radius +
                        perpendicular_x * lateral,
                    desired[1] + config.height * 0.5F,
                    desired[2] +
                        direction_z * config.radius +
                        perpendicular_z * lateral
                };
                if (segment_intersects_triangle(
                        ray_start,
                        ray_end,
                        triangle)) {
                    const auto current_ground = find_ground(
                        {
                            start[0],
                            start[2],
                            start[1],
                            config.maximum_step_up,
                            config.maximum_drop
                        });
                    auto blocked_position = start;
                    if (current_ground.has_value()) {
                        blocked_position[1] =
                            current_ground->height;
                    }
                    return core::Result<
                        GroundedMotion,
                        StaticCollisionError>::success(
                        GroundedMotion{
                            blocked_position,
                            current_ground.has_value(),
                            true
                        });
                }
            }
        }
    }
    return core::Result<
        GroundedMotion,
        StaticCollisionError>::success(
        GroundedMotion{desired, true, false});
}

std::size_t StaticCollisionWorld::triangle_count() const noexcept {
    return triangles_.size();
}

}

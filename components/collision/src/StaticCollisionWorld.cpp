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
constexpr float kBroadphaseCellSize = 256.0F;
constexpr std::int64_t kMaximumCellsPerTriangle = 64;

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

std::optional<std::int32_t> cell_coordinate(float value) {
    const auto coordinate = std::floor(
        static_cast<double>(value) /
        static_cast<double>(kBroadphaseCellSize));
    if (!std::isfinite(coordinate) ||
        coordinate <
            static_cast<double>(
                std::numeric_limits<std::int32_t>::min()) ||
        coordinate >
            static_cast<double>(
                std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(coordinate);
}

std::uint64_t cell_key(
    std::int32_t x,
    std::int32_t z) {
    return
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(x))
         << 32U) |
        static_cast<std::uint32_t>(z);
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
    : triangles_(std::move(triangles)) {
    for (std::size_t index = 0;
         index < triangles_.size();
         ++index) {
        const auto& triangle = triangles_[index];
        const auto minimum_x = (std::min)(
            triangle.a[0],
            (std::min)(triangle.b[0], triangle.c[0]));
        const auto maximum_x = (std::max)(
            triangle.a[0],
            (std::max)(triangle.b[0], triangle.c[0]));
        const auto minimum_z = (std::min)(
            triangle.a[2],
            (std::min)(triangle.b[2], triangle.c[2]));
        const auto maximum_z = (std::max)(
            triangle.a[2],
            (std::max)(triangle.b[2], triangle.c[2]));
        const auto first_x = cell_coordinate(minimum_x);
        const auto last_x = cell_coordinate(maximum_x);
        const auto first_z = cell_coordinate(minimum_z);
        const auto last_z = cell_coordinate(maximum_z);
        if (!first_x.has_value() ||
            !last_x.has_value() ||
            !first_z.has_value() ||
            !last_z.has_value()) {
            global_triangles_.push_back(index);
            continue;
        }
        const auto cells_x =
            static_cast<std::int64_t>(last_x.value()) -
            static_cast<std::int64_t>(first_x.value()) + 1;
        const auto cells_z =
            static_cast<std::int64_t>(last_z.value()) -
            static_cast<std::int64_t>(first_z.value()) + 1;
        if (cells_x <= 0 ||
            cells_z <= 0 ||
            cells_x > kMaximumCellsPerTriangle ||
            cells_z >
                kMaximumCellsPerTriangle / cells_x) {
            global_triangles_.push_back(index);
            continue;
        }
        for (auto x = static_cast<std::int64_t>(
                 first_x.value());
             x <= static_cast<std::int64_t>(last_x.value());
             ++x) {
            for (auto z = static_cast<std::int64_t>(
                     first_z.value());
                 z <= static_cast<std::int64_t>(last_z.value());
                 ++z) {
                triangles_by_cell_[
                    cell_key(
                        static_cast<std::int32_t>(x),
                        static_cast<std::int32_t>(z))]
                    .push_back(index);
            }
        }
    }
}

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
    const auto consider =
        [this, &query, &best](std::size_t index) {
        const auto& triangle = triangles_[index];
        const auto normal_length = length(triangle.normal);
        if (normal_length <= kEpsilon ||
            std::fabs(triangle.normal[1]) / normal_length <
                kMinimumWalkableNormal) {
            return;
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
            return;
        }
        if (!best.has_value() || height > best->height) {
            best = GroundContact{height, index};
        }
    };
    for (const auto index : global_triangles_) {
        consider(index);
    }
    const auto cell_x = cell_coordinate(query.x);
    const auto cell_z = cell_coordinate(query.z);
    if (cell_x.has_value() && cell_z.has_value()) {
        const auto found = triangles_by_cell_.find(
            cell_key(cell_x.value(), cell_z.value()));
        if (found != triangles_by_cell_.end()) {
            for (const auto index : found->second) {
                consider(index);
            }
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

    const auto blocking_wall =
        [this, &config](
            const std::array<float, 3>& from,
            const std::array<float, 3>& to)
            -> const Triangle* {
        const auto delta_x = to[0] - from[0];
        const auto delta_z = to[2] - from[2];
        const auto horizontal_length =
            std::sqrt(delta_x * delta_x + delta_z * delta_z);
        if (horizontal_length <= kEpsilon) {
            return nullptr;
        }
        const auto direction_x = delta_x / horizontal_length;
        const auto direction_z = delta_z / horizontal_length;
        const auto perpendicular_x = -direction_z;
        const auto perpendicular_z = direction_x;
        const auto intersects =
            [&from,
             &to,
             &config,
             direction_x,
             direction_z,
             perpendicular_x,
             perpendicular_z](
                const Triangle& triangle) {
            const auto normal_length = length(triangle.normal);
            if (normal_length <= kEpsilon ||
                std::fabs(triangle.normal[1]) / normal_length >
                    kMinimumWalkableNormal) {
                return false;
            }
            for (const float lateral : {
                     -config.radius,
                     0.0F,
                     config.radius}) {
                const std::array<float, 3> ray_start{
                    from[0] +
                        direction_x * config.radius +
                        perpendicular_x * lateral,
                    from[1] + config.height * 0.5F,
                    from[2] +
                        direction_z * config.radius +
                        perpendicular_z * lateral
                };
                const std::array<float, 3> ray_end{
                    to[0] +
                        direction_x * config.radius +
                        perpendicular_x * lateral,
                    to[1] + config.height * 0.5F,
                    to[2] +
                        direction_z * config.radius +
                        perpendicular_z * lateral
                };
                if (segment_intersects_triangle(
                        ray_start,
                        ray_end,
                        triangle)) {
                    return true;
                }
            }
            return false;
        };
        for (const auto index : global_triangles_) {
            if (intersects(triangles_[index])) {
                return &triangles_[index];
            }
        }
        const auto first_x = cell_coordinate(
            (std::min)(from[0], to[0]) - config.radius);
        const auto last_x = cell_coordinate(
            (std::max)(from[0], to[0]) + config.radius);
        const auto first_z = cell_coordinate(
            (std::min)(from[2], to[2]) - config.radius);
        const auto last_z = cell_coordinate(
            (std::max)(from[2], to[2]) + config.radius);
        if (!first_x.has_value() ||
            !last_x.has_value() ||
            !first_z.has_value() ||
            !last_z.has_value()) {
            return nullptr;
        }
        for (auto x = static_cast<std::int64_t>(
                 first_x.value());
             x <= static_cast<std::int64_t>(last_x.value());
             ++x) {
            for (auto z = static_cast<std::int64_t>(
                     first_z.value());
                 z <= static_cast<std::int64_t>(last_z.value());
                 ++z) {
                const auto found = triangles_by_cell_.find(
                    cell_key(
                        static_cast<std::int32_t>(x),
                        static_cast<std::int32_t>(z)));
                if (found == triangles_by_cell_.end()) {
                    continue;
                }
                for (const auto index : found->second) {
                    if (intersects(triangles_[index])) {
                        return &triangles_[index];
                    }
                }
            }
        }
        return nullptr;
    };

    const auto* wall = blocking_wall(start, desired);
    if (wall != nullptr) {
        const auto horizontal_normal_length = std::sqrt(
            wall->normal[0] * wall->normal[0] +
            wall->normal[2] * wall->normal[2]);
        auto slid = start;
        if (horizontal_normal_length > kEpsilon) {
            const auto normal_x =
                wall->normal[0] / horizontal_normal_length;
            const auto normal_z =
                wall->normal[2] / horizontal_normal_length;
            const auto delta_x = desired[0] - start[0];
            const auto delta_z = desired[2] - start[2];
            const auto into_wall =
                delta_x * normal_x + delta_z * normal_z;
            slid[0] += delta_x - into_wall * normal_x;
            slid[2] += delta_z - into_wall * normal_z;
            const auto slide_ground = find_ground(
                {
                    slid[0],
                    slid[2],
                    start[1],
                    config.maximum_step_up,
                    config.maximum_drop
                });
            if (slide_ground.has_value()) {
                slid[1] = slide_ground->height;
                if (blocking_wall(start, slid) == nullptr) {
                    return core::Result<
                        GroundedMotion,
                        StaticCollisionError>::success(
                        GroundedMotion{slid, true, true});
                }
            }
        }
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
            blocked_position[1] = current_ground->height;
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
    return core::Result<
        GroundedMotion,
        StaticCollisionError>::success(
        GroundedMotion{desired, true, false});
}

std::size_t StaticCollisionWorld::triangle_count() const noexcept {
    return triangles_.size();
}

}

#pragma once

#include <contract/core/Result.hpp>
#include <contract/scene/CollisionScene.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace contract::collision {

enum class StaticCollisionErrorCode {
    invalid_vertex,
    invalid_topology,
    index_out_of_range,
    degenerate_triangle,
    invalid_configuration
};

struct StaticCollisionError {
    StaticCollisionErrorCode code{
        StaticCollisionErrorCode::invalid_topology};
    std::string message;
};

struct GroundQuery {
    float x{0.0F};
    float z{0.0F};
    float reference_height{0.0F};
    float maximum_step_up{0.0F};
    float maximum_drop{0.0F};
};

struct GroundContact {
    float height{0.0F};
    std::size_t triangle_index{0};
};

struct GroundedMotionConfig {
    float radius{0.0F};
    float height{0.0F};
    float maximum_step_up{0.0F};
    float maximum_drop{0.0F};
};

struct GroundedMotion {
    std::array<float, 3> position{};
    bool grounded{false};
    bool blocked{false};
};

class StaticCollisionWorld {
public:
    [[nodiscard]] static core::Result<
        StaticCollisionWorld,
        StaticCollisionError>
    create(scene::CollisionScene scene);

    [[nodiscard]] std::optional<GroundContact> find_ground(
        const GroundQuery& query) const noexcept;

    [[nodiscard]] core::Result<
        GroundedMotion,
        StaticCollisionError>
    resolve_grounded_motion(
        std::array<float, 3> start,
        std::array<float, 3> desired,
        const GroundedMotionConfig& config) const;

    [[nodiscard]] std::size_t triangle_count() const noexcept;

private:
    struct Triangle {
        std::array<float, 3> a;
        std::array<float, 3> b;
        std::array<float, 3> c;
        std::array<float, 3> normal;
    };

    explicit StaticCollisionWorld(std::vector<Triangle> triangles);

    std::vector<Triangle> triangles_;
    std::unordered_map<
        std::uint64_t,
        std::vector<std::size_t>>
        triangles_by_cell_;
    std::vector<std::size_t> global_triangles_;
};

}

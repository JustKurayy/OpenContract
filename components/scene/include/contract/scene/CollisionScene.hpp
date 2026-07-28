#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace contract::scene {

struct CollisionVertex {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct CollisionScene {
    std::vector<CollisionVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t source_mesh_count{0};
};

}

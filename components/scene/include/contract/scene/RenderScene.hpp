#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace contract::scene {

struct RenderVertex {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct RenderScene {
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t source_mesh_count{0};
};

}

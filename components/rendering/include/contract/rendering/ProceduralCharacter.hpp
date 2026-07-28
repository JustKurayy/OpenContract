#pragma once

#include <contract/scene/RenderScene.hpp>

#include <cstdint>
#include <vector>

namespace contract::rendering {

struct ProceduralCharacterMesh {
    std::vector<scene::RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
};

[[nodiscard]] ProceduralCharacterMesh create_procedural_character();

}

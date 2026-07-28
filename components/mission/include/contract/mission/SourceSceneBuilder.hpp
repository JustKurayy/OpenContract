#pragma once

#include <contract/core/Result.hpp>
#include <contract/formats/GmsSceneDecoder.hpp>
#include <contract/formats/MaterialDatabaseDecoder.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ScenePlacementDecoder.hpp>
#include <contract/formats/TextureDatabaseDecoder.hpp>
#include <contract/scene/RenderScene.hpp>

#include <cstddef>
#include <span>
#include <string>

namespace contract::mission {

enum class SourceSceneBuildErrorCode {
    invalid_transform,
    invalid_hierarchy,
    scene_limit_exceeded,
    no_renderable_placements
};

struct SourceSceneBuildError {
    SourceSceneBuildErrorCode code{
        SourceSceneBuildErrorCode::no_renderable_placements};
    std::string message;
};

struct SourceSceneBuildLimits {
    std::size_t max_vertices{8'000'000};
    std::size_t max_indices{24'000'000};
};

struct SourceSceneBuildResources {
    const formats::MaterialDatabase* materials{nullptr};
    const formats::TextureDatabase* textures{nullptr};
    std::span<const std::byte> texture_bytes;
};

struct SourceSceneBuildResult {
    scene::RenderScene render_scene;
    std::size_t active_placements{0};
    std::size_t inactive_placements{0};
    std::size_t inherited_inactive_placements{0};
    std::size_t invisible_placements{0};
    std::size_t empty_placements{0};
    std::size_t missing_placements{0};
    std::size_t visibility_group_count{0};
    std::size_t collision_meshes{0};
    std::size_t overlay_meshes{0};
};

class SourceSceneBuilder {
public:
    [[nodiscard]] static core::Result<
        SourceSceneBuildResult,
        SourceSceneBuildError>
    build(
        std::span<const formats::PrimitiveMesh> meshes,
        std::span<const formats::ScenePlacement> placements,
        std::span<const formats::GmsSceneNode> hierarchy = {},
        SourceSceneBuildResources resources = {},
        SourceSceneBuildLimits limits = {});
};

}

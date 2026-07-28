#include <contract/mission/SourceSceneBuilder.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace contract::mission {
namespace {

core::Result<SourceSceneBuildResult, SourceSceneBuildError> failure(
    SourceSceneBuildErrorCode code,
    std::string message) {
    return core::Result<
        SourceSceneBuildResult,
        SourceSceneBuildError>::failure(
        {code, std::move(message)});
}

scene::RenderVertex transform_position(
    const formats::PrimitivePosition& source,
    const formats::ScenePlacement& placement) {
    return {
        placement.matrix[0] * source.x +
            placement.matrix[3] * source.y +
            placement.matrix[6] * source.z +
            placement.position[0],
        placement.matrix[1] * source.x +
            placement.matrix[4] * source.y +
            placement.matrix[7] * source.z +
            placement.position[1],
        placement.matrix[2] * source.x +
            placement.matrix[5] * source.y +
            placement.matrix[8] * source.z +
            placement.position[2]
    };
}

bool valid_placement(
    const formats::ScenePlacement& placement) {
    for (const auto value : placement.matrix) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    for (const auto value : placement.position) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

}

core::Result<SourceSceneBuildResult, SourceSceneBuildError>
SourceSceneBuilder::build(
    std::span<const formats::PrimitiveMesh> meshes,
    std::span<const formats::ScenePlacement> placements,
    SourceSceneBuildLimits limits) {
    std::unordered_map<
        std::uint32_t,
        std::vector<const formats::PrimitiveMesh*>>
        meshes_by_model;
    meshes_by_model.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        meshes_by_model[mesh.model_record].push_back(&mesh);
    }

    SourceSceneBuildResult result;
    for (const auto& placement : placements) {
        if (!valid_placement(placement)) {
            return failure(
                SourceSceneBuildErrorCode::invalid_transform,
                "Source scene placement contains a non-finite transform");
        }
        if (placement.inactive) {
            ++result.inactive_placements;
            continue;
        }
        if (placement.primitive_record == 0) {
            ++result.empty_placements;
            continue;
        }
        const auto found =
            meshes_by_model.find(placement.primitive_record);
        if (found == meshes_by_model.end()) {
            ++result.missing_placements;
            continue;
        }

        ++result.active_placements;
        for (const auto* mesh : found->second) {
            if (mesh->positions.size() >
                    limits.max_vertices -
                        result.render_scene.vertices.size() ||
                mesh->indices.size() >
                    limits.max_indices -
                        result.render_scene.indices.size()) {
                return failure(
                    SourceSceneBuildErrorCode::scene_limit_exceeded,
                    "Placed source scene exceeds configured limits");
            }
            if (result.render_scene.vertices.size() >
                std::numeric_limits<std::uint32_t>::max()) {
                return failure(
                    SourceSceneBuildErrorCode::scene_limit_exceeded,
                    "Placed source scene vertex index exceeds renderer limits");
            }
            const auto base_vertex = static_cast<std::uint32_t>(
                result.render_scene.vertices.size());
            for (const auto& position : mesh->positions) {
                const auto transformed =
                    transform_position(position, placement);
                if (!std::isfinite(transformed.x) ||
                    !std::isfinite(transformed.y) ||
                    !std::isfinite(transformed.z)) {
                    return failure(
                        SourceSceneBuildErrorCode::invalid_transform,
                        "Source scene transform produced a non-finite vertex");
                }
                result.render_scene.vertices.push_back(transformed);
            }
            for (const auto index : mesh->indices) {
                if (index >
                    std::numeric_limits<std::uint32_t>::max() -
                        base_vertex) {
                    return failure(
                        SourceSceneBuildErrorCode::scene_limit_exceeded,
                        "Placed source scene index would overflow");
                }
                result.render_scene.indices.push_back(
                    base_vertex + index);
            }
            ++result.render_scene.source_mesh_count;
        }
    }

    if (result.render_scene.vertices.empty() ||
        result.render_scene.indices.empty()) {
        return failure(
            SourceSceneBuildErrorCode::no_renderable_placements,
            "Source scene contains no supported active placements");
    }
    return core::Result<
        SourceSceneBuildResult,
        SourceSceneBuildError>::success(
        std::move(result));
}

}

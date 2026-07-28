#include <contract/mission/SourceSceneBuilder.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string_view>
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
            placement.position[2],
        0.0F,
        0.0F
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

bool is_preferred_spawn(
    std::string_view node_name,
    const SourceSceneBuildHints& hints) {
    return std::find(
               hints.preferred_spawn_nodes.begin(),
               hints.preferred_spawn_nodes.end(),
               node_name) != hints.preferred_spawn_nodes.end();
}

formats::ScenePlacement compose(
    const formats::ScenePlacement& parent,
    const formats::ScenePlacement& local) {
    formats::ScenePlacement world = local;
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 3; ++inner) {
                value +=
                    parent.matrix[row + inner * 3U] *
                    local.matrix[inner + column * 3U];
            }
            world.matrix[row + column * 3U] = value;
        }
    }
    for (std::size_t row = 0; row < 3; ++row) {
        world.position[row] = parent.position[row];
        for (std::size_t inner = 0; inner < 3; ++inner) {
            world.position[row] +=
                parent.matrix[row + inner * 3U] *
                local.position[inner];
        }
    }
    return world;
}

std::optional<std::uint32_t> diffuse_texture(
    const formats::PrimitiveMesh& mesh,
    const SourceSceneBuildResources& resources) {
    if (resources.materials == nullptr ||
        resources.textures == nullptr) {
        return std::nullopt;
    }
    const auto* material =
        resources.materials->find(mesh.material_id);
    if (material == nullptr ||
        !material->diffuse_texture_id.has_value()) {
        return std::nullopt;
    }
    const auto* texture = resources.textures->find(
        material->diffuse_texture_id.value());
    if (texture == nullptr ||
        texture->format == formats::TextureFormat::unsupported ||
        !texture->first_mip(resources.texture_bytes).has_value()) {
        return std::nullopt;
    }
    return texture->texture_id;
}

scene::RenderTextureFormat render_format(
    formats::TextureFormat format) {
    switch (format) {
    case formats::TextureFormat::bc1:
        return scene::RenderTextureFormat::bc1;
    case formats::TextureFormat::bc2:
        return scene::RenderTextureFormat::bc2;
    case formats::TextureFormat::rgba8:
        return scene::RenderTextureFormat::rgba8;
    case formats::TextureFormat::unsupported:
        break;
    }
    return scene::RenderTextureFormat::rgba8;
}

void apply_material_state(
    scene::RenderBatch& batch,
    const formats::MaterialDefinition* material) {
    if (material == nullptr) {
        return;
    }
    switch (material->blend_mode) {
    case formats::MaterialBlendMode::opaque:
        batch.blend_mode = scene::RenderBlendMode::opaque;
        break;
    case formats::MaterialBlendMode::alpha:
        batch.blend_mode = scene::RenderBlendMode::alpha;
        break;
    case formats::MaterialBlendMode::additive:
        batch.blend_mode = scene::RenderBlendMode::additive;
        break;
    }
    switch (material->cull_mode) {
    case formats::MaterialCullMode::unspecified:
        batch.cull_mode = scene::RenderCullMode::unspecified;
        break;
    case formats::MaterialCullMode::one_sided:
        batch.cull_mode = scene::RenderCullMode::one_sided;
        break;
    case formats::MaterialCullMode::two_sided:
        batch.cull_mode = scene::RenderCullMode::two_sided;
        break;
    }
    batch.opacity = material->opacity;
    if (material->alpha_test_enabled) {
        batch.alpha_reference =
            static_cast<float>(material->alpha_reference) / 255.0F;
    }
}

}

core::Result<SourceSceneBuildResult, SourceSceneBuildError>
SourceSceneBuilder::build(
    std::span<const formats::PrimitiveMesh> meshes,
    std::span<const formats::ScenePlacement> placements,
    std::span<const formats::GmsSceneNode> hierarchy,
    SourceSceneBuildResources resources,
    SourceSceneBuildHints hints,
    SourceSceneBuildLimits limits) {
    std::unordered_map<
        std::uint32_t,
        std::vector<const formats::PrimitiveMesh*>>
        meshes_by_model;
    meshes_by_model.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        meshes_by_model[mesh.model_record].push_back(&mesh);
    }

    if (!hierarchy.empty() && hierarchy.size() != placements.size()) {
        return failure(
            SourceSceneBuildErrorCode::invalid_hierarchy,
            "Source scene hierarchy and property counts do not match");
    }

    std::vector<formats::ScenePlacement> world_placements;
    world_placements.reserve(placements.size());
    std::vector<bool> inherited_inactive;
    inherited_inactive.reserve(placements.size());
    SourceSceneBuildResult result;
    using BatchKey = std::pair<
        std::uint16_t,
        std::optional<std::uint32_t>>;
    std::map<BatchKey, std::vector<std::uint32_t>> indices_by_material;
    std::size_t total_index_count = 0;
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const auto& local = placements[index];
        if (!valid_placement(local)) {
            return failure(
                SourceSceneBuildErrorCode::invalid_transform,
                "Source scene placement contains a non-finite transform");
        }
        auto world = local;
        bool parent_inactive = false;
        if (!hierarchy.empty()) {
            if (hierarchy[index].visibility_group_root && index != 0U) {
                ++result.visibility_group_count;
            }
            if (hierarchy[index].parent_index.has_value()) {
                const auto parent = hierarchy[index].parent_index.value();
                if (parent >= index) {
                    return failure(
                        SourceSceneBuildErrorCode::invalid_hierarchy,
                        "Source scene parent must precede its child");
                }
                world = compose(world_placements[parent], local);
                parent_inactive =
                    placements[parent].inactive ||
                    inherited_inactive[parent];
            } else if (index != 0U) {
                return failure(
                    SourceSceneBuildErrorCode::invalid_hierarchy,
                    "Source scene contains a detached non-root node");
            }
        }
        world_placements.push_back(world);
        inherited_inactive.push_back(parent_inactive);
    }

    for (std::size_t index = 0; index < placements.size(); ++index) {
        const auto& local = placements[index];
        const auto& placement = world_placements[index];
        if (!valid_placement(placement)) {
            return failure(
                SourceSceneBuildErrorCode::invalid_transform,
                "Source scene placement contains a non-finite transform");
        }
        if (local.inactive) {
            ++result.inactive_placements;
            continue;
        }
        if (inherited_inactive[index]) {
            ++result.inherited_inactive_placements;
            continue;
        }
        if (local.invisible) {
            ++result.invisible_placements;
            continue;
        }
        if (!result.preferred_spawn.has_value() &&
            !hierarchy.empty() &&
            is_preferred_spawn(hierarchy[index].name, hints)) {
            scene::Transform spawn;
            spawn.position = placement.position;
            result.preferred_spawn = spawn;
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
            const auto* material =
                resources.materials == nullptr
                    ? nullptr
                    : resources.materials->find(mesh->material_id);
            if (material != nullptr && material->collision_only) {
                ++result.collision_meshes;
                continue;
            }
            if (material != nullptr && material->overlay_only) {
                ++result.overlay_meshes;
                continue;
            }
            if (mesh->positions.size() >
                    limits.max_vertices -
                        result.render_scene.vertices.size() ||
                mesh->indices.size() >
                    limits.max_indices -
                        total_index_count) {
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
            if (!mesh->texture_coordinates.empty()) {
                if (mesh->texture_coordinates.size() !=
                    mesh->positions.size()) {
                    return failure(
                        SourceSceneBuildErrorCode::invalid_transform,
                        "Source mesh texture-coordinate count is inconsistent");
                }
                for (std::size_t vertex = 0;
                     vertex < mesh->texture_coordinates.size();
                     ++vertex) {
                    result.render_scene
                        .vertices[base_vertex + vertex]
                        .u = mesh->texture_coordinates[vertex].u;
                    result.render_scene
                        .vertices[base_vertex + vertex]
                        .v = mesh->texture_coordinates[vertex].v;
                }
            }
            auto& batch_indices =
                indices_by_material[
                    {
                        mesh->material_id,
                        diffuse_texture(*mesh, resources)
                    }];
            for (const auto source_index : mesh->indices) {
                if (source_index >
                    std::numeric_limits<std::uint32_t>::max() -
                        base_vertex) {
                    return failure(
                        SourceSceneBuildErrorCode::scene_limit_exceeded,
                        "Placed source scene index would overflow");
                }
                batch_indices.push_back(
                    base_vertex + source_index);
            }
            total_index_count += mesh->indices.size();
            ++result.render_scene.source_mesh_count;
        }
    }

    if (result.render_scene.vertices.empty() ||
        indices_by_material.empty()) {
        return failure(
            SourceSceneBuildErrorCode::no_renderable_placements,
            "Source scene contains no supported active placements");
    }
    std::unordered_map<std::uint32_t, std::size_t>
        texture_indices;
    texture_indices.reserve(indices_by_material.size());
    for (auto& [key, batch_indices] :
         indices_by_material) {
        if (batch_indices.empty()) {
            continue;
        }
        if (result.render_scene.indices.size() >
                std::numeric_limits<std::uint32_t>::max() ||
            batch_indices.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            return failure(
                SourceSceneBuildErrorCode::scene_limit_exceeded,
                "Source scene batch exceeds renderer limits");
        }
        scene::RenderBatch batch;
        batch.first_index = static_cast<std::uint32_t>(
            result.render_scene.indices.size());
        batch.index_count = static_cast<std::uint32_t>(
            batch_indices.size());
        batch.source_material_id = key.first;
        const formats::MaterialDefinition* material = nullptr;
        if (resources.materials != nullptr) {
            material = resources.materials->find(key.first);
        }
        apply_material_state(batch, material);
        result.render_scene.indices.insert(
            result.render_scene.indices.end(),
            batch_indices.begin(),
            batch_indices.end());
        if (key.second.has_value()) {
            if (resources.textures == nullptr) {
                return failure(
                    SourceSceneBuildErrorCode::invalid_transform,
                    "Source texture reference has no texture database");
            }
            const auto* texture =
                resources.textures->find(key.second.value());
            if (texture == nullptr) {
                return failure(
                    SourceSceneBuildErrorCode::invalid_transform,
                    "Source texture reference is missing");
            }
            const auto mip =
                texture->first_mip(resources.texture_bytes);
            if (!mip.has_value()) {
                return failure(
                    SourceSceneBuildErrorCode::invalid_transform,
                    "Source texture data range is invalid");
            }
            const auto existing =
                texture_indices.find(texture->texture_id);
            if (existing != texture_indices.end()) {
                batch.texture_index = existing->second;
            } else {
                batch.texture_index =
                    result.render_scene.textures.size();
                texture_indices.emplace(
                    texture->texture_id,
                    batch.texture_index.value());
                result.render_scene.textures.push_back(
                    {
                        texture->texture_id,
                        texture->width,
                        texture->height,
                        render_format(texture->format),
                        std::vector<std::byte>(
                            mip.value().begin(),
                            mip.value().end())
                    });
            }
        }
        result.render_scene.batches.push_back(batch);
    }
    return core::Result<
        SourceSceneBuildResult,
        SourceSceneBuildError>::success(
        std::move(result));
}

}

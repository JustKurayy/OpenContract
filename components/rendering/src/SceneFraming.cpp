#include <contract/rendering/SceneFraming.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace contract::rendering {
namespace {

struct Bounds {
    float minimum_x{std::numeric_limits<float>::max()};
    float minimum_y{std::numeric_limits<float>::max()};
    float minimum_z{std::numeric_limits<float>::max()};
    float maximum_x{std::numeric_limits<float>::lowest()};
    float maximum_y{std::numeric_limits<float>::lowest()};
    float maximum_z{std::numeric_limits<float>::lowest()};
    bool populated{false};
};

bool include_vertex(
    Bounds& bounds,
    const scene::RenderVertex& vertex) noexcept {
    if (!std::isfinite(vertex.x) ||
        !std::isfinite(vertex.y) ||
        !std::isfinite(vertex.z)) {
        return false;
    }
    bounds.minimum_x = std::min(bounds.minimum_x, vertex.x);
    bounds.minimum_y = std::min(bounds.minimum_y, vertex.y);
    bounds.minimum_z = std::min(bounds.minimum_z, vertex.z);
    bounds.maximum_x = std::max(bounds.maximum_x, vertex.x);
    bounds.maximum_y = std::max(bounds.maximum_y, vertex.y);
    bounds.maximum_z = std::max(bounds.maximum_z, vertex.z);
    bounds.populated = true;
    return true;
}

std::optional<SceneFrame> make_frame(const Bounds& bounds) noexcept {
    if (!bounds.populated) {
        return std::nullopt;
    }
    const CameraPoint center{
        (bounds.minimum_x + bounds.maximum_x) * 0.5F,
        (bounds.minimum_y + bounds.maximum_y) * 0.5F,
        (bounds.minimum_z + bounds.maximum_z) * 0.5F
    };
    const auto extent_x = bounds.maximum_x - bounds.minimum_x;
    const auto extent_y = bounds.maximum_y - bounds.minimum_y;
    const auto extent_z = bounds.maximum_z - bounds.minimum_z;
    const auto radius = std::max(
        1.0F,
        0.5F * std::sqrt(
            extent_x * extent_x +
            extent_y * extent_y +
            extent_z * extent_z));
    return SceneFrame{center, radius};
}

std::optional<SceneFrame> make_robust_frame(
    const scene::RenderScene& scene,
    const std::vector<bool>& referenced) {
    std::vector<float> coordinates_x;
    std::vector<float> coordinates_y;
    std::vector<float> coordinates_z;
    coordinates_x.reserve(scene.vertices.size());
    coordinates_y.reserve(scene.vertices.size());
    coordinates_z.reserve(scene.vertices.size());
    for (std::size_t index = 0;
         index < scene.vertices.size();
         ++index) {
        if (!referenced[index]) {
            continue;
        }
        const auto& vertex = scene.vertices[index];
        if (!std::isfinite(vertex.x) ||
            !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z)) {
            return std::nullopt;
        }
        coordinates_x.push_back(vertex.x);
        coordinates_y.push_back(vertex.y);
        coordinates_z.push_back(vertex.z);
    }
    if (coordinates_x.empty()) {
        return std::nullopt;
    }
    if (coordinates_x.size() < 20U) {
        Bounds bounds;
        for (std::size_t index = 0;
             index < scene.vertices.size();
             ++index) {
            if (referenced[index]) {
                static_cast<void>(
                    include_vertex(bounds, scene.vertices[index]));
            }
        }
        return make_frame(bounds);
    }

    const auto lower_index = coordinates_x.size() / 4U;
    const auto upper_index =
        coordinates_x.size() - lower_index - 1U;
    const auto bounds_for = [lower_index, upper_index](
                                std::vector<float>& values) {
        std::nth_element(
            values.begin(),
            values.begin() + lower_index,
            values.end());
        const auto minimum = values[lower_index];
        std::nth_element(
            values.begin() + lower_index,
            values.begin() + upper_index,
            values.end());
        return std::pair{minimum, values[upper_index]};
    };
    const auto [minimum_x, maximum_x] = bounds_for(coordinates_x);
    const auto [minimum_y, maximum_y] = bounds_for(coordinates_y);
    const auto [minimum_z, maximum_z] = bounds_for(coordinates_z);
    return make_frame(
        {
            minimum_x,
            minimum_y,
            minimum_z,
            maximum_x,
            maximum_y,
            maximum_z,
            true
        });
}

}

std::optional<SceneFrame> choose_initial_scene_frame(
    const scene::RenderScene& scene) noexcept {
    std::vector<bool> textured_vertices(
        scene.vertices.size(),
        false);
    for (const auto& batch : scene.batches) {
        if (!batch.texture_index.has_value() ||
            batch.first_index > scene.indices.size() ||
            batch.index_count >
                scene.indices.size() - batch.first_index) {
            continue;
        }
        for (std::uint32_t offset = 0;
             offset < batch.index_count;
             ++offset) {
            const auto vertex_index =
                scene.indices[batch.first_index + offset];
            if (vertex_index >= scene.vertices.size()) {
                return std::nullopt;
            }
            textured_vertices[vertex_index] = true;
        }
    }
    if (const auto textured =
            make_robust_frame(scene, textured_vertices);
        textured.has_value()) {
        return textured;
    }

    Bounds full_bounds;
    for (const auto& vertex : scene.vertices) {
        if (!include_vertex(full_bounds, vertex)) {
            return std::nullopt;
        }
    }
    return make_frame(full_bounds);
}

}

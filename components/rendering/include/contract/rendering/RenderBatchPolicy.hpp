#pragma once

#include <contract/scene/RenderScene.hpp>

#include <span>
#include <vector>

namespace contract::rendering {

[[nodiscard]] std::vector<scene::RenderBatch> order_render_batches(
    std::span<const scene::RenderBatch> batches);

[[nodiscard]] bool render_batch_writes_depth(
    const scene::RenderBatch& batch) noexcept;

}

#include <contract/rendering/RenderBatchPolicy.hpp>

#include <algorithm>

namespace contract::rendering {
namespace {

int render_priority(const scene::RenderBatch& batch) noexcept {
    if (batch.layer == scene::RenderLayer::background) {
        return 0;
    }
    switch (batch.blend_mode) {
    case scene::RenderBlendMode::opaque:
        return 1;
    case scene::RenderBlendMode::alpha:
        return 2;
    case scene::RenderBlendMode::additive:
        return 3;
    }
    return 1;
}

}

std::vector<scene::RenderBatch> order_render_batches(
    std::span<const scene::RenderBatch> batches) {
    std::vector<scene::RenderBatch> ordered(
        batches.begin(),
        batches.end());
    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const auto& left, const auto& right) {
            return render_priority(left) < render_priority(right);
        });
    return ordered;
}

bool render_batch_writes_depth(
    const scene::RenderBatch& batch) noexcept {
    return batch.layer == scene::RenderLayer::world &&
           batch.blend_mode == scene::RenderBlendMode::opaque;
}

}

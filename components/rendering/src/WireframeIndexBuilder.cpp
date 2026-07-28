#include <contract/rendering/WireframeIndexBuilder.hpp>

#include <cstddef>
#include <limits>
#include <utility>

namespace contract::rendering {

core::Result<std::vector<std::uint32_t>, WireframeIndexError>
build_wireframe_indices(
    std::span<const std::uint32_t> triangle_indices) {
    if (triangle_indices.size() % 3 != 0) {
        return core::Result<
            std::vector<std::uint32_t>,
            WireframeIndexError>::failure(
            {WireframeIndexErrorCode::incomplete_triangle});
    }
    if (triangle_indices.size() >
        std::numeric_limits<std::size_t>::max() / 2) {
        return core::Result<
            std::vector<std::uint32_t>,
            WireframeIndexError>::failure(
            {WireframeIndexErrorCode::overflow});
    }

    std::vector<std::uint32_t> result;
    result.reserve(triangle_indices.size() * 2);
    for (std::size_t offset = 0;
         offset < triangle_indices.size();
         offset += 3) {
        const auto first = triangle_indices[offset];
        const auto second = triangle_indices[offset + 1];
        const auto third = triangle_indices[offset + 2];
        result.insert(
            result.end(),
            {
                first, second,
                second, third,
                third, first
            });
    }
    return core::Result<
        std::vector<std::uint32_t>,
        WireframeIndexError>::success(std::move(result));
}

}

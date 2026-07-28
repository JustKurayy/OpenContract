#pragma once

#include <contract/core/Result.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace contract::rendering {

enum class WireframeIndexErrorCode {
    incomplete_triangle,
    overflow
};

struct WireframeIndexError {
    WireframeIndexErrorCode code{
        WireframeIndexErrorCode::incomplete_triangle};
};

[[nodiscard]] core::Result<
    std::vector<std::uint32_t>,
    WireframeIndexError>
build_wireframe_indices(
    std::span<const std::uint32_t> triangle_indices);

}

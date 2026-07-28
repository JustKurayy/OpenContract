#pragma once

#include <contract/core/Result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace contract::formats {

enum class ScenePlacementErrorCode {
    truncated,
    invalid_header,
    unsupported_layout,
    invalid_instruction,
    limit_exceeded,
    no_placements
};

struct ScenePlacementError {
    ScenePlacementErrorCode code{
        ScenePlacementErrorCode::invalid_header};
    std::uint64_t offset{0};
    std::string message;
};

struct ScenePlacementDecodeLimits {
    std::size_t max_file_size{256U * 1024U * 1024U};
    std::size_t max_tokens{1'000'000};
    std::size_t max_definitions{1'000'000};
    std::size_t max_instructions{2'000'000};
    std::size_t max_placements{1'000'000};
};

struct ScenePlacement {
    std::uint32_t primitive_record{0};
    std::array<float, 9> matrix{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F
    };
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    bool inactive{false};
    bool invisible{false};
    std::uint64_t byte_offset{0};
};

struct ScenePlacementDecodeResult {
    std::uint32_t declared_objects{0};
    std::vector<ScenePlacement> placements;
};

class ScenePlacementDecoder {
public:
    [[nodiscard]] static core::Result<
        ScenePlacementDecodeResult,
        ScenePlacementError>
    decode(
        std::span<const std::byte> bytes,
        ScenePlacementDecodeLimits limits = {});
};

}

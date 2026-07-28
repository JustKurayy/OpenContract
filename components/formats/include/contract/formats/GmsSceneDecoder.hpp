#pragma once

#include <contract/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace contract::formats {

enum class GmsSceneErrorCode {
    truncated,
    invalid_header,
    decompression_failed,
    invalid_offset,
    invalid_hierarchy,
    limit_exceeded
};

struct GmsSceneError {
    GmsSceneErrorCode code{GmsSceneErrorCode::invalid_header};
    std::uint64_t offset{0};
    std::string message;
};

struct GmsSceneDecodeLimits {
    std::size_t max_packed_size{64U * 1024U * 1024U};
    std::size_t max_unpacked_size{256U * 1024U * 1024U};
    std::size_t max_nodes{1'000'000};
    std::size_t max_name_length{1024};
};

struct GmsSceneNode {
    std::string name;
    std::uint32_t primitive_record{0};
    std::uint32_t type_id{0};
    std::optional<std::size_t> parent_index;
    std::uint8_t relative_depth{0};
    bool visibility_group_root{false};
    std::uint64_t byte_offset{0};
};

struct GmsSceneDecodeResult {
    std::vector<GmsSceneNode> nodes;
    std::size_t visibility_group_count{0};
};

class GmsSceneDecoder {
public:
    [[nodiscard]] static core::Result<
        GmsSceneDecodeResult,
        GmsSceneError>
    decode(
        std::span<const std::byte> packed_gms,
        std::span<const std::byte> name_buffer,
        GmsSceneDecodeLimits limits = {});
};

}

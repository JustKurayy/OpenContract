#pragma once

#include <contract/core/Result.hpp>
#include <contract/formats/AnimationDatabaseDecoder.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace contract::formats {

struct AnimationTrackEncoding {
    std::uint16_t track_id{0};
    std::uint8_t encoding{0};
};

enum class AnimationChannelLayout {
    routed_tracks,
    opaque_payload
};

struct AnimationChannelDirectory {
    std::uint8_t channel_mask{0};
    std::uint32_t encoded_offset{0};
    std::uint32_t encoded_size{0};
    std::uint32_t payload_offset{0};
    std::vector<std::byte> encoded_value_bytes;
    std::vector<AnimationTrackEncoding> tracks;
    AnimationChannelLayout layout{
        AnimationChannelLayout::routed_tracks};
};

[[nodiscard]] core::Result<
    std::vector<AnimationChannelDirectory>,
    AnimationDatabaseDecodeError>
decode_animation_track_directories(
    std::span<const std::byte> encoded_clip,
    const AnimationClipDescriptor& descriptor,
    std::size_t max_total_tracks = 4096);

}

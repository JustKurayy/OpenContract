#pragma once

#include <contract/core/Result.hpp>
#include <contract/formats/AnimationDatabaseDecoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace contract::formats {

struct AnimationTrackEncoding {
    std::uint16_t track_id{0};
    std::uint8_t encoding{0};
};

struct AnimationChannelDirectory {
    std::uint8_t channel_slot{0};
    std::uint32_t encoded_offset{0};
    std::uint32_t encoded_size{0};
    std::uint32_t payload_offset{0};
    std::array<std::byte, 20> leading_bytes{};
    std::vector<AnimationTrackEncoding> tracks;
};

[[nodiscard]] core::Result<
    std::vector<AnimationChannelDirectory>,
    AnimationDatabaseDecodeError>
decode_animation_track_directories(
    std::span<const std::byte> encoded_clip,
    const AnimationClipDescriptor& descriptor,
    std::size_t max_total_tracks = 4096);

}

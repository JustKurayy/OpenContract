#include <contract/formats/AnimationTrackDirectory.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace contract::formats {
namespace {

constexpr std::size_t kLeadingByteCount = 20;
constexpr std::size_t kTrackCountSize = 1;
constexpr std::size_t kTrackIdSize = 2;
constexpr std::size_t kEncodingSize = 1;

struct ChannelReference {
    std::uint8_t slot{0};
    std::uint32_t offset{0};
};

core::Result<
    std::vector<AnimationChannelDirectory>,
    AnimationDatabaseDecodeError>
failure(
    AnimationDatabaseDecodeErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<
        std::vector<AnimationChannelDirectory>,
        AnimationDatabaseDecodeError>::failure(
        {code, offset, std::move(message)});
}

std::uint16_t read_u16(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(bytes[offset]) |
        (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

bool supported_encoding(
    std::uint8_t channel_slot,
    std::uint8_t encoding) {
    switch (channel_slot) {
    case 0:
        return encoding == 0xe0U ||
            encoding == 0xe2U ||
            encoding == 0xe3U;
    case 1:
        return encoding == 0x84U ||
            encoding == 0xb5U ||
            encoding == 0xe6U;
    case 2:
        return encoding == 0xe4U ||
            encoding == 0xe5U ||
            encoding == 0xe6U;
    case 3:
        return encoding == 0x91U;
    default:
        return false;
    }
}

}

core::Result<
    std::vector<AnimationChannelDirectory>,
    AnimationDatabaseDecodeError>
decode_animation_track_directories(
    std::span<const std::byte> encoded_clip,
    const AnimationClipDescriptor& descriptor,
    std::size_t max_total_tracks) {
    if (encoded_clip.size() != descriptor.encoded_size) {
        return failure(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            0,
            "Animation clip payload size does not match its descriptor");
    }

    std::vector<ChannelReference> references;
    references.reserve(descriptor.channel_offsets.size());
    std::unordered_set<std::uint32_t> offsets;
    for (std::size_t slot = 0;
         slot < descriptor.channel_offsets.size();
         ++slot) {
        const auto offset = descriptor.channel_offsets[slot];
        if (!offset.has_value()) {
            continue;
        }
        if (offset.value() >= encoded_clip.size()) {
            return failure(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                offset.value(),
                "Animation channel directory offset is out of range");
        }
        if (!offsets.insert(offset.value()).second) {
            return failure(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                offset.value(),
                "Animation channel directory offsets must be unique");
        }
        references.push_back(
            {
                static_cast<std::uint8_t>(slot),
                offset.value()
            });
    }
    if (references.empty()) {
        return failure(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            0,
            "Animation clip has no channel directories");
    }
    std::sort(
        references.begin(),
        references.end(),
        [](const ChannelReference& left, const ChannelReference& right) {
            return left.offset < right.offset;
        });

    std::vector<AnimationChannelDirectory> directories;
    directories.reserve(references.size());
    std::size_t total_tracks = 0;
    for (std::size_t index = 0;
         index < references.size();
         ++index) {
        const auto start =
            static_cast<std::size_t>(references[index].offset);
        const auto end = index + 1U < references.size()
            ? static_cast<std::size_t>(references[index + 1U].offset)
            : encoded_clip.size();
        if (end < start ||
            end - start < kLeadingByteCount + kTrackCountSize) {
            return failure(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                start,
                "Animation channel directory is truncated");
        }

        const auto track_count = std::to_integer<std::uint8_t>(
            encoded_clip[start + kLeadingByteCount]);
        if (track_count == 0U) {
            return failure(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                start + kLeadingByteCount,
                "Animation channel directory has no tracks");
        }
        const auto track_count_size =
            static_cast<std::size_t>(track_count);
        if (track_count_size >
            (std::numeric_limits<std::size_t>::max() -
             kLeadingByteCount - kTrackCountSize) /
                (kTrackIdSize + kEncodingSize)) {
            return failure(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                start + kLeadingByteCount,
                "Animation channel directory size would overflow");
        }
        const auto directory_size =
            kLeadingByteCount + kTrackCountSize +
            track_count_size * (kTrackIdSize + kEncodingSize);
        if (directory_size > end - start) {
            return failure(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                start + kLeadingByteCount,
                "Animation channel directory exceeds its encoded range");
        }
        const auto directory_end = start + directory_size;
        if (total_tracks > max_total_tracks ||
            track_count_size > max_total_tracks - total_tracks) {
            return failure(
                AnimationDatabaseDecodeErrorCode::limit_exceeded,
                start + kLeadingByteCount,
                "Animation track routes exceed configured limits");
        }
        total_tracks += track_count_size;

        AnimationChannelDirectory directory;
        directory.channel_slot = references[index].slot;
        directory.encoded_offset =
            static_cast<std::uint32_t>(start);
        directory.encoded_size =
            static_cast<std::uint32_t>(end - start);
        directory.payload_offset =
            static_cast<std::uint32_t>(directory_end);
        std::copy_n(
            encoded_clip.begin() + start,
            kLeadingByteCount,
            directory.leading_bytes.begin());
        directory.tracks.reserve(track_count_size);
        std::unordered_set<std::uint16_t> track_ids;
        const auto identifiers =
            start + kLeadingByteCount + kTrackCountSize;
        const auto encodings =
            identifiers + track_count_size * kTrackIdSize;
        for (std::size_t track = 0;
             track < track_count_size;
             ++track) {
            const auto track_id = read_u16(
                encoded_clip,
                identifiers + track * kTrackIdSize);
            if (!track_ids.insert(track_id).second) {
                return failure(
                    AnimationDatabaseDecodeErrorCode::invalid_clip,
                    identifiers + track * kTrackIdSize,
                    "Animation channel track identifiers must be unique");
            }
            const auto encoding = std::to_integer<std::uint8_t>(
                encoded_clip[encodings + track]);
            if (!supported_encoding(
                    directory.channel_slot,
                    encoding)) {
                return failure(
                    AnimationDatabaseDecodeErrorCode::
                        unsupported_track_encoding,
                    encodings + track,
                    "Animation track encoding is unsupported");
            }
            directory.tracks.push_back(
                {
                    track_id,
                    encoding
                });
        }
        directories.push_back(std::move(directory));
    }

    return core::Result<
        std::vector<AnimationChannelDirectory>,
        AnimationDatabaseDecodeError>::success(std::move(directories));
}

}

#include <contract/formats/AnimationDatabaseDecoder.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace contract::formats {
namespace {

constexpr std::uint64_t kHeaderSize = 16;
constexpr std::uint64_t kChunkHeaderSize = 8;
constexpr std::uint32_t kChunkSizeMask = 0x7fffffffU;
constexpr std::uint32_t kDatabaseChunkKind = 6;
constexpr std::uint32_t kPathChunkKind = 7;
constexpr std::uint32_t kClipChunkKind = 4;
constexpr std::uint64_t kClipHeaderSize = 32;
constexpr std::uint64_t kClipDescriptorSize = 64;
constexpr std::uint32_t kMissingOffset =
    std::numeric_limits<std::uint32_t>::max();

struct Chunk {
    std::uint64_t offset{0};
    std::uint64_t size{0};
    std::uint32_t kind{0};
};

core::Result<
    AnimationDatabaseIndex,
    AnimationDatabaseDecodeError>
failure(
    AnimationDatabaseDecodeErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<
        AnimationDatabaseIndex,
        AnimationDatabaseDecodeError>::failure(
        {code, offset, std::move(message)});
}

template <typename T>
core::Result<T, AnimationDatabaseDecodeError> value_failure(
    AnimationDatabaseDecodeErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<T, AnimationDatabaseDecodeError>::failure(
        {code, offset, std::move(message)});
}

std::uint32_t read_u32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(
                     bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

core::Result<
    std::vector<std::byte>,
    AnimationDatabaseDecodeError>
read_bytes(
    const datasource::IReadOnlyDataSource& source,
    std::uint64_t offset,
    std::size_t count,
    datasource::ReadBudget& budget,
    std::string message) {
    auto bytes = source.read(offset, count, budget);
    if (!bytes.has_value()) {
        auto code = AnimationDatabaseDecodeErrorCode::source_error;
        if (bytes.error().code ==
            datasource::DataSourceErrorCode::end_of_source) {
            code = AnimationDatabaseDecodeErrorCode::truncated;
        } else if (
            bytes.error().code ==
            datasource::DataSourceErrorCode::offset_overflow) {
            code = AnimationDatabaseDecodeErrorCode::invalid_chunk;
        }
        return core::Result<
            std::vector<std::byte>,
            AnimationDatabaseDecodeError>::failure(
            {
                code,
                bytes.error().offset,
                std::move(message)
            });
    }
    return core::Result<
        std::vector<std::byte>,
        AnimationDatabaseDecodeError>::success(
        std::move(bytes.value()));
}

core::Result<std::string, AnimationDatabaseDecodeError>
read_string(
    const std::vector<std::byte>& bytes,
    std::size_t offset,
    std::size_t limit,
    std::uint64_t source_offset,
    AnimationDatabaseDecodeErrorCode code,
    std::string message) {
    if (offset >= bytes.size()) {
        return value_failure<std::string>(
            code,
            source_offset + offset,
            std::move(message));
    }
    const auto available = bytes.size() - offset;
    const auto maximum = (std::min)(available, limit + 1U);
    std::size_t length = 0;
    while (length < maximum &&
           bytes[offset + length] != std::byte{0}) {
        ++length;
    }
    if (length == 0U ||
        length == maximum ||
        bytes[offset + length] != std::byte{0}) {
        return value_failure<std::string>(
            code,
            source_offset + offset,
            std::move(message));
    }
    std::string value;
    value.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        value.push_back(static_cast<char>(
            std::to_integer<unsigned char>(
                bytes[offset + index])));
    }
    return core::Result<
        std::string,
        AnimationDatabaseDecodeError>::success(std::move(value));
}

core::Result<
    std::vector<AnimationPath>,
    AnimationDatabaseDecodeError>
decode_paths(
    const datasource::IReadOnlyDataSource& source,
    const Chunk& chunk,
    datasource::ReadBudget& budget,
    const AnimationDatabaseDecodeLimits& limits) {
    if (chunk.size - kChunkHeaderSize >
        limits.max_metadata_chunk_size) {
        return value_failure<std::vector<AnimationPath>>(
            AnimationDatabaseDecodeErrorCode::limit_exceeded,
            chunk.offset,
            "Animation path metadata exceeds configured limits");
    }
    auto bytes = read_bytes(
        source,
        chunk.offset + kChunkHeaderSize,
        static_cast<std::size_t>(chunk.size - kChunkHeaderSize),
        budget,
        "Animation path metadata could not be read");
    if (!bytes.has_value()) {
        return core::Result<
            std::vector<AnimationPath>,
            AnimationDatabaseDecodeError>::failure(bytes.error());
    }
    if (bytes.value().size() < 4U) {
        return value_failure<std::vector<AnimationPath>>(
            AnimationDatabaseDecodeErrorCode::invalid_path,
            chunk.offset + kChunkHeaderSize,
            "Animation path metadata is truncated");
    }
    const auto count = read_u32(bytes.value(), 0);
    if (count > limits.max_paths) {
        return value_failure<std::vector<AnimationPath>>(
            AnimationDatabaseDecodeErrorCode::limit_exceeded,
            chunk.offset + kChunkHeaderSize,
            "Animation path count exceeds configured limits");
    }
    const auto count64 = static_cast<std::uint64_t>(count);
    if (count64 >
        (std::numeric_limits<std::uint64_t>::max() - 4U) / 8U) {
        return value_failure<std::vector<AnimationPath>>(
            AnimationDatabaseDecodeErrorCode::invalid_path,
            chunk.offset + kChunkHeaderSize,
            "Animation path table size would overflow");
    }
    const auto table_size = 4U + count64 * 8U;
    if (table_size > bytes.value().size()) {
        return value_failure<std::vector<AnimationPath>>(
            AnimationDatabaseDecodeErrorCode::invalid_path,
            chunk.offset + kChunkHeaderSize,
            "Animation path table exceeds its chunk");
    }

    std::vector<AnimationPath> paths;
    paths.reserve(count);
    std::unordered_set<std::uint32_t> identifiers;
    std::unordered_set<std::string> names;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto entry = 4U + static_cast<std::size_t>(index) * 8U;
        const auto string_offset = read_u32(bytes.value(), entry);
        const auto identifier = read_u32(bytes.value(), entry + 4U);
        if (string_offset >
            bytes.value().size() - table_size) {
            return value_failure<std::vector<AnimationPath>>(
                AnimationDatabaseDecodeErrorCode::invalid_path,
                chunk.offset + kChunkHeaderSize + entry,
                "Animation path string offset is out of range");
        }
        const auto absolute =
            static_cast<std::size_t>(table_size + string_offset);
        auto path = read_string(
            bytes.value(),
            absolute,
            limits.max_string_length,
            chunk.offset + kChunkHeaderSize,
            AnimationDatabaseDecodeErrorCode::invalid_path,
            "Animation path is empty or unterminated");
        if (!path.has_value()) {
            return core::Result<
                std::vector<AnimationPath>,
                AnimationDatabaseDecodeError>::failure(path.error());
        }
        if (!identifiers.insert(identifier).second ||
            !names.insert(path.value()).second) {
            return value_failure<std::vector<AnimationPath>>(
                AnimationDatabaseDecodeErrorCode::duplicate_path,
                chunk.offset + kChunkHeaderSize + entry,
                "Animation path identifiers and names must be unique");
        }
        paths.push_back({identifier, std::move(path.value())});
    }
    return core::Result<
        std::vector<AnimationPath>,
        AnimationDatabaseDecodeError>::success(std::move(paths));
}

core::Result<AnimationDatabase, AnimationDatabaseDecodeError>
decode_database(
    const datasource::IReadOnlyDataSource& source,
    const Chunk& chunk,
    datasource::ReadBudget& budget,
    const AnimationDatabaseDecodeLimits& limits) {
    if (chunk.size - kChunkHeaderSize >
        limits.max_metadata_chunk_size) {
        return value_failure<AnimationDatabase>(
            AnimationDatabaseDecodeErrorCode::limit_exceeded,
            chunk.offset,
            "Animation database metadata exceeds configured limits");
    }
    auto bytes = read_bytes(
        source,
        chunk.offset + kChunkHeaderSize,
        static_cast<std::size_t>(chunk.size - kChunkHeaderSize),
        budget,
        "Animation database metadata could not be read");
    if (!bytes.has_value()) {
        return core::Result<
            AnimationDatabase,
            AnimationDatabaseDecodeError>::failure(bytes.error());
    }
    auto name = read_string(
        bytes.value(),
        0,
        limits.max_string_length,
        chunk.offset + kChunkHeaderSize,
        AnimationDatabaseDecodeErrorCode::invalid_database,
        "Animation database name is empty or unterminated");
    if (!name.has_value()) {
        return core::Result<
            AnimationDatabase,
            AnimationDatabaseDecodeError>::failure(name.error());
    }
    auto position = name.value().size() + 1U;
    if (position >
        std::numeric_limits<std::size_t>::max() - 3U) {
        return value_failure<AnimationDatabase>(
            AnimationDatabaseDecodeErrorCode::invalid_database,
            chunk.offset + kChunkHeaderSize,
            "Animation database alignment would overflow");
    }
    position = (position + 3U) & ~std::size_t{3U};
    if (position > bytes.value().size() ||
        bytes.value().size() - position < 4U) {
        return value_failure<AnimationDatabase>(
            AnimationDatabaseDecodeErrorCode::invalid_database,
            chunk.offset + kChunkHeaderSize + position,
            "Animation database mapping count is truncated");
    }
    const auto count = read_u32(bytes.value(), position);
    if (count > limits.max_paths) {
        return value_failure<AnimationDatabase>(
            AnimationDatabaseDecodeErrorCode::limit_exceeded,
            chunk.offset + kChunkHeaderSize + position,
            "Animation database mapping count exceeds configured limits");
    }
    const auto remaining = bytes.value().size() - position - 4U;
    if (static_cast<std::uint64_t>(count) >
        remaining / 8U) {
        return value_failure<AnimationDatabase>(
            AnimationDatabaseDecodeErrorCode::invalid_database,
            chunk.offset + kChunkHeaderSize + position,
            "Animation database mapping table is truncated");
    }

    AnimationDatabase database;
    database.name = std::move(name.value());
    database.clip_indices.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto entry = position + 4U +
            static_cast<std::size_t>(index) * 8U;
        const auto clip = read_u32(bytes.value(), entry + 4U);
        database.clip_indices.push_back(
            clip == kMissingOffset
                ? std::nullopt
                : std::optional<std::uint32_t>{clip});
    }
    return core::Result<
        AnimationDatabase,
        AnimationDatabaseDecodeError>::success(std::move(database));
}

core::Result<
    std::vector<AnimationClipDescriptor>,
    AnimationDatabaseDecodeError>
decode_clips(
    const datasource::IReadOnlyDataSource& source,
    const Chunk& chunk,
    datasource::ReadBudget& budget,
    const AnimationDatabaseDecodeLimits& limits) {
    if (chunk.size - kChunkHeaderSize < kClipHeaderSize) {
        return value_failure<std::vector<AnimationClipDescriptor>>(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            chunk.offset + kChunkHeaderSize,
            "Animation clip header is truncated");
    }
    auto header = read_bytes(
        source,
        chunk.offset + kChunkHeaderSize,
        static_cast<std::size_t>(kClipHeaderSize),
        budget,
        "Animation clip header could not be read");
    if (!header.has_value()) {
        return core::Result<
            std::vector<AnimationClipDescriptor>,
            AnimationDatabaseDecodeError>::failure(header.error());
    }
    const auto count = read_u32(header.value(), 0);
    const auto encoded_data_size = read_u32(header.value(), 4);
    const auto repeated_count = read_u32(header.value(), 24);
    if (count != repeated_count) {
        return value_failure<std::vector<AnimationClipDescriptor>>(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            chunk.offset + kChunkHeaderSize + 24U,
            "Animation clip counts are inconsistent");
    }
    if (count > limits.max_clips) {
        return value_failure<std::vector<AnimationClipDescriptor>>(
            AnimationDatabaseDecodeErrorCode::limit_exceeded,
            chunk.offset + kChunkHeaderSize,
            "Animation clip count exceeds configured limits");
    }
    const auto count64 = static_cast<std::uint64_t>(count);
    if (count64 >
        std::numeric_limits<std::uint64_t>::max() /
            kClipDescriptorSize) {
        return value_failure<std::vector<AnimationClipDescriptor>>(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            chunk.offset + kChunkHeaderSize,
            "Animation clip descriptor size would overflow");
    }
    const auto descriptor_bytes = count64 * kClipDescriptorSize;
    const auto metadata_size = kClipHeaderSize + descriptor_bytes;
    const auto payload_size = chunk.size - kChunkHeaderSize;
    if (metadata_size > payload_size ||
        encoded_data_size > payload_size - metadata_size) {
        return value_failure<std::vector<AnimationClipDescriptor>>(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            chunk.offset + kChunkHeaderSize,
            "Animation clip payload exceeds its chunk");
    }
    if (descriptor_bytes >
        limits.max_metadata_chunk_size) {
        return value_failure<std::vector<AnimationClipDescriptor>>(
            AnimationDatabaseDecodeErrorCode::limit_exceeded,
            chunk.offset + kChunkHeaderSize,
            "Animation clip descriptors exceed configured limits");
    }
    auto descriptors = read_bytes(
        source,
        chunk.offset + kChunkHeaderSize + kClipHeaderSize,
        static_cast<std::size_t>(descriptor_bytes),
        budget,
        "Animation clip descriptors could not be read");
    if (!descriptors.has_value()) {
        return core::Result<
            std::vector<AnimationClipDescriptor>,
            AnimationDatabaseDecodeError>::failure(
            descriptors.error());
    }
    const auto data_offset =
        chunk.offset + kChunkHeaderSize + metadata_size;

    std::vector<AnimationClipDescriptor> clips;
    clips.reserve(count);
    constexpr std::array<std::size_t, 4> pointer_fields{
        36U,
        40U,
        44U,
        52U
    };
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto offset =
            static_cast<std::size_t>(index) *
            static_cast<std::size_t>(kClipDescriptorSize);
        const auto packed_samples =
            read_u32(descriptors.value(), offset + 24U);
        const auto packed_tracks =
            read_u32(descriptors.value(), offset + 28U);
        const auto encoded_size =
            read_u32(descriptors.value(), offset + 32U);
        const auto sample_count = static_cast<std::uint16_t>(
            packed_samples & 0xffffU);
        const auto samples_per_second =
            static_cast<std::uint16_t>(
                packed_samples >> 16U);
        const auto track_count = static_cast<std::uint16_t>(
            packed_tracks & 0xffffU);
        const auto track_flags = static_cast<std::uint16_t>(
            packed_tracks >> 16U);
        if (sample_count == 0U ||
            samples_per_second == 0U ||
            track_count == 0U) {
            return value_failure<
                std::vector<AnimationClipDescriptor>>(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                data_offset - descriptor_bytes + offset + 24U,
                "Animation clip timing or track count is zero");
        }
        if (track_count > limits.max_tracks_per_clip ||
            encoded_size > limits.max_encoded_clip_size) {
            return value_failure<
                std::vector<AnimationClipDescriptor>>(
                AnimationDatabaseDecodeErrorCode::limit_exceeded,
                data_offset - descriptor_bytes + offset + 28U,
                "Animation clip exceeds configured limits");
        }

        std::array<std::optional<std::uint32_t>, 4> pointers;
        std::optional<std::uint32_t> minimum;
        for (std::size_t pointer_index = 0;
             pointer_index < pointer_fields.size();
             ++pointer_index) {
            const auto pointer = read_u32(
                descriptors.value(),
                offset + pointer_fields[pointer_index]);
            if (pointer == kMissingOffset) {
                continue;
            }
            if (pointer > encoded_data_size) {
                return value_failure<
                    std::vector<AnimationClipDescriptor>>(
                    AnimationDatabaseDecodeErrorCode::invalid_clip,
                    data_offset - descriptor_bytes + offset +
                        pointer_fields[pointer_index],
                    "Animation clip channel offset is out of range");
            }
            pointers[pointer_index] = pointer;
            minimum = !minimum.has_value()
                ? std::optional<std::uint32_t>{pointer}
                : std::optional<std::uint32_t>{
                      (std::min)(minimum.value(), pointer)};
        }
        if (!minimum.has_value() ||
            minimum.value() > encoded_data_size ||
            encoded_size > encoded_data_size - minimum.value()) {
            return value_failure<
                std::vector<AnimationClipDescriptor>>(
                AnimationDatabaseDecodeErrorCode::invalid_clip,
                data_offset - descriptor_bytes + offset + 32U,
                "Animation clip encoded range is invalid");
        }
        for (auto& pointer : pointers) {
            if (!pointer.has_value()) {
                continue;
            }
            if (pointer.value() < minimum.value() ||
                pointer.value() - minimum.value() > encoded_size) {
                return value_failure<
                    std::vector<AnimationClipDescriptor>>(
                    AnimationDatabaseDecodeErrorCode::invalid_clip,
                    data_offset - descriptor_bytes + offset + 36U,
                    "Animation clip channel lies outside its encoded range");
            }
            pointer = pointer.value() - minimum.value();
        }
        clips.push_back(
            {
                index,
                sample_count,
                samples_per_second,
                track_count,
                track_flags,
                encoded_size,
                data_offset + minimum.value(),
                pointers
            });
    }
    return core::Result<
        std::vector<AnimationClipDescriptor>,
        AnimationDatabaseDecodeError>::success(std::move(clips));
}

}

float AnimationClipDescriptor::duration_seconds() const noexcept {
    return samples_per_second == 0U
        ? 0.0F
        : static_cast<float>(sample_count) /
              static_cast<float>(samples_per_second);
}

AnimationDatabaseIndex::AnimationDatabaseIndex(
    std::vector<AnimationPath> paths,
    std::vector<AnimationDatabase> databases,
    std::vector<AnimationClipDescriptor> clips)
    : paths_(std::move(paths)),
      databases_(std::move(databases)),
      clips_(std::move(clips)) {}

core::Result<
    AnimationDatabaseIndex,
    AnimationDatabaseDecodeError>
AnimationDatabaseIndex::read(
    const datasource::IReadOnlyDataSource& source,
    datasource::ReadBudget& budget,
    AnimationDatabaseDecodeLimits limits) {
    if (source.size() < kHeaderSize) {
        return failure(
            AnimationDatabaseDecodeErrorCode::truncated,
            0,
            "Animation database header is truncated");
    }
    auto header = read_bytes(
        source,
        0,
        static_cast<std::size_t>(kHeaderSize),
        budget,
        "Animation database header could not be read");
    if (!header.has_value()) {
        return core::Result<
            AnimationDatabaseIndex,
            AnimationDatabaseDecodeError>::failure(header.error());
    }
    if (header.value()[0] != std::byte{'M'} ||
        header.value()[1] != std::byte{'N'} ||
        header.value()[2] != std::byte{'A'} ||
        header.value()[3] != std::byte{0}) {
        return failure(
            AnimationDatabaseDecodeErrorCode::invalid_header,
            0,
            "Animation database signature is unsupported");
    }
    const auto declared_size =
        read_u32(header.value(), 4) & kChunkSizeMask;
    const auto repeated_size = read_u32(header.value(), 8);
    const auto chunk_count = read_u32(header.value(), 12);
    if (declared_size != repeated_size ||
        repeated_size != source.size()) {
        return failure(
            AnimationDatabaseDecodeErrorCode::invalid_header,
            4,
            "Animation database sizes are inconsistent");
    }
    if (chunk_count > limits.max_chunks) {
        return failure(
            AnimationDatabaseDecodeErrorCode::limit_exceeded,
            12,
            "Animation chunk count exceeds configured limits");
    }

    std::vector<Chunk> chunks;
    chunks.reserve(chunk_count);
    std::uint64_t chunk_offset = kHeaderSize;
    for (std::uint32_t index = 0; index < chunk_count; ++index) {
        if (chunk_offset > source.size() ||
            source.size() - chunk_offset < kChunkHeaderSize) {
            return failure(
                AnimationDatabaseDecodeErrorCode::truncated,
                chunk_offset,
                "Animation chunk header is truncated");
        }
        auto bytes = read_bytes(
            source,
            chunk_offset,
            static_cast<std::size_t>(kChunkHeaderSize),
            budget,
            "Animation chunk header could not be read");
        if (!bytes.has_value()) {
            return core::Result<
                AnimationDatabaseIndex,
                AnimationDatabaseDecodeError>::failure(bytes.error());
        }
        const auto kind = read_u32(bytes.value(), 0);
        const auto size =
            read_u32(bytes.value(), 4) & kChunkSizeMask;
        if (size < kChunkHeaderSize ||
            size > source.size() - chunk_offset) {
            return failure(
                AnimationDatabaseDecodeErrorCode::invalid_chunk,
                chunk_offset + 4U,
                "Animation chunk size is invalid");
        }
        chunks.push_back({chunk_offset, size, kind});
        chunk_offset += size;
    }
    if (chunk_offset != source.size()) {
        return failure(
            AnimationDatabaseDecodeErrorCode::invalid_chunk,
            chunk_offset,
            "Animation chunks do not cover the declared file size");
    }

    std::optional<std::vector<AnimationPath>> paths;
    std::optional<std::vector<AnimationClipDescriptor>> clips;
    std::vector<AnimationDatabase> databases;
    std::unordered_set<std::string> database_names;
    for (const auto& chunk : chunks) {
        if (chunk.kind == kPathChunkKind) {
            if (paths.has_value()) {
                return failure(
                    AnimationDatabaseDecodeErrorCode::invalid_chunk,
                    chunk.offset,
                    "Animation database contains multiple path chunks");
            }
            auto decoded = decode_paths(
                source,
                chunk,
                budget,
                limits);
            if (!decoded.has_value()) {
                return core::Result<
                    AnimationDatabaseIndex,
                    AnimationDatabaseDecodeError>::failure(
                    decoded.error());
            }
            paths = std::move(decoded.value());
        } else if (chunk.kind == kDatabaseChunkKind) {
            if (databases.size() >= limits.max_databases) {
                return failure(
                    AnimationDatabaseDecodeErrorCode::limit_exceeded,
                    chunk.offset,
                    "Animation database count exceeds configured limits");
            }
            auto decoded = decode_database(
                source,
                chunk,
                budget,
                limits);
            if (!decoded.has_value()) {
                return core::Result<
                    AnimationDatabaseIndex,
                    AnimationDatabaseDecodeError>::failure(
                    decoded.error());
            }
            if (!database_names.insert(decoded.value().name).second) {
                return failure(
                    AnimationDatabaseDecodeErrorCode::duplicate_database,
                    chunk.offset,
                    "Animation database names must be unique");
            }
            databases.push_back(std::move(decoded.value()));
        } else if (chunk.kind == kClipChunkKind) {
            if (clips.has_value()) {
                return failure(
                    AnimationDatabaseDecodeErrorCode::invalid_chunk,
                    chunk.offset,
                    "Animation database contains multiple clip chunks");
            }
            auto decoded = decode_clips(
                source,
                chunk,
                budget,
                limits);
            if (!decoded.has_value()) {
                return core::Result<
                    AnimationDatabaseIndex,
                    AnimationDatabaseDecodeError>::failure(
                    decoded.error());
            }
            clips = std::move(decoded.value());
        }
    }
    if (!paths.has_value() ||
        !clips.has_value() ||
        databases.empty()) {
        return failure(
            AnimationDatabaseDecodeErrorCode::invalid_chunk,
            0,
            "Animation database is missing required metadata chunks");
    }
    for (const auto& database : databases) {
        for (const auto& clip : database.clip_indices) {
            if (clip.has_value() &&
                clip.value() >= clips.value().size()) {
                return failure(
                    AnimationDatabaseDecodeErrorCode::invalid_database,
                    0,
                    "Animation database references an invalid clip");
            }
        }
    }
    return core::Result<
        AnimationDatabaseIndex,
        AnimationDatabaseDecodeError>::success(
        AnimationDatabaseIndex(
            std::move(paths.value()),
            std::move(databases),
            std::move(clips.value())));
}

const std::vector<AnimationPath>&
AnimationDatabaseIndex::paths() const noexcept {
    return paths_;
}

const std::vector<AnimationDatabase>&
AnimationDatabaseIndex::databases() const noexcept {
    return databases_;
}

const std::vector<AnimationClipDescriptor>&
AnimationDatabaseIndex::clips() const noexcept {
    return clips_;
}

core::Result<
    AnimationClipDescriptor,
    AnimationDatabaseDecodeError>
AnimationDatabaseIndex::resolve(
    std::string_view database_name,
    std::string_view path) const {
    const auto database = std::find_if(
        databases_.begin(),
        databases_.end(),
        [database_name](const AnimationDatabase& candidate) {
            return candidate.name == database_name;
        });
    if (database == databases_.end()) {
        return value_failure<AnimationClipDescriptor>(
            AnimationDatabaseDecodeErrorCode::database_not_found,
            0,
            "Animation database name was not found");
    }
    const auto named_path = std::find_if(
        paths_.begin(),
        paths_.end(),
        [path](const AnimationPath& candidate) {
            return candidate.path == path;
        });
    if (named_path == paths_.end()) {
        return value_failure<AnimationClipDescriptor>(
            AnimationDatabaseDecodeErrorCode::path_not_found,
            0,
            "Animation path was not found");
    }
    if (named_path->id >= database->clip_indices.size() ||
        !database->clip_indices[named_path->id].has_value()) {
        return value_failure<AnimationClipDescriptor>(
            AnimationDatabaseDecodeErrorCode::clip_unavailable,
            named_path->id,
            "Animation path is unavailable in the selected database");
    }
    const auto clip =
        database->clip_indices[named_path->id].value();
    if (clip >= clips_.size()) {
        return value_failure<AnimationClipDescriptor>(
            AnimationDatabaseDecodeErrorCode::invalid_database,
            clip,
            "Animation database resolved an invalid clip");
    }
    return core::Result<
        AnimationClipDescriptor,
        AnimationDatabaseDecodeError>::success(clips_[clip]);
}

core::Result<
    std::vector<std::byte>,
    AnimationDatabaseDecodeError>
AnimationDatabaseIndex::read_encoded_clip(
    const datasource::IReadOnlyDataSource& source,
    const AnimationClipDescriptor& clip,
    datasource::ReadBudget& budget) const {
    if (clip.index >= clips_.size()) {
        return value_failure<std::vector<std::byte>>(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            clip.index,
            "Animation clip index is out of range");
    }
    const auto& indexed = clips_[clip.index];
    if (indexed.encoded_offset != clip.encoded_offset ||
        indexed.encoded_size != clip.encoded_size) {
        return value_failure<std::vector<std::byte>>(
            AnimationDatabaseDecodeErrorCode::invalid_clip,
            clip.index,
            "Animation clip descriptor does not belong to this index");
    }
    return read_bytes(
        source,
        clip.encoded_offset,
        clip.encoded_size,
        budget,
        "Animation clip payload could not be read");
}

}

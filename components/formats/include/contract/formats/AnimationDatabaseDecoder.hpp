#pragma once

#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace contract::formats {

enum class AnimationDatabaseDecodeErrorCode {
    truncated,
    invalid_header,
    invalid_chunk,
    invalid_path,
    invalid_database,
    invalid_clip,
    unsupported_track_encoding,
    duplicate_path,
    duplicate_database,
    database_not_found,
    path_not_found,
    clip_unavailable,
    limit_exceeded,
    source_error
};

struct AnimationDatabaseDecodeError {
    AnimationDatabaseDecodeErrorCode code{
        AnimationDatabaseDecodeErrorCode::invalid_header};
    std::uint64_t offset{0};
    std::string message;
};

struct AnimationDatabaseDecodeLimits {
    std::size_t max_chunks{256};
    std::size_t max_paths{16'384};
    std::size_t max_databases{256};
    std::size_t max_clips{16'384};
    std::size_t max_tracks_per_clip{2048};
    std::size_t max_string_length{1024};
    std::uint64_t max_metadata_chunk_size{16U * 1024U * 1024U};
    std::uint64_t max_encoded_clip_size{64U * 1024U * 1024U};
};

struct AnimationPath {
    std::uint32_t id{0};
    std::string path;
};

struct AnimationDatabase {
    std::string name;
    std::vector<std::optional<std::uint32_t>> clip_indices;
};

struct AnimationClipDescriptor {
    std::uint32_t index{0};
    std::uint16_t sample_count{0};
    std::uint16_t samples_per_second{0};
    std::uint16_t track_count{0};
    std::uint16_t track_flags{0};
    std::uint32_t encoded_size{0};
    std::uint64_t encoded_offset{0};
    std::array<std::optional<std::uint32_t>, 4> channel_offsets;

    [[nodiscard]] float duration_seconds() const noexcept;
};

class AnimationDatabaseIndex {
public:
    [[nodiscard]] static core::Result<
        AnimationDatabaseIndex,
        AnimationDatabaseDecodeError>
    read(
        const datasource::IReadOnlyDataSource& source,
        datasource::ReadBudget& budget,
        AnimationDatabaseDecodeLimits limits = {});

    [[nodiscard]] const std::vector<AnimationPath>& paths() const noexcept;
    [[nodiscard]] const std::vector<AnimationDatabase>&
    databases() const noexcept;
    [[nodiscard]] const std::vector<AnimationClipDescriptor>&
    clips() const noexcept;

    [[nodiscard]] core::Result<
        AnimationClipDescriptor,
        AnimationDatabaseDecodeError>
    resolve(
        std::string_view database_name,
        std::string_view path) const;

    [[nodiscard]] core::Result<
        std::vector<std::byte>,
        AnimationDatabaseDecodeError>
    read_encoded_clip(
        const datasource::IReadOnlyDataSource& source,
        const AnimationClipDescriptor& clip,
        datasource::ReadBudget& budget) const;

private:
    AnimationDatabaseIndex(
        std::vector<AnimationPath> paths,
        std::vector<AnimationDatabase> databases,
        std::vector<AnimationClipDescriptor> clips);

    std::vector<AnimationPath> paths_;
    std::vector<AnimationDatabase> databases_;
    std::vector<AnimationClipDescriptor> clips_;
};

}

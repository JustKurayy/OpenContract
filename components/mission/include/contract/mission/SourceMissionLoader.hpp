#pragma once

#include <contract/core/Result.hpp>
#include <contract/scene/RenderScene.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace contract::mission {

enum class SourceMissionLoadErrorCode {
    invalid_identifier,
    archive_unavailable,
    archive_invalid,
    primitive_entry_missing,
    property_entry_missing,
    primitive_container_invalid,
    property_decode_failed,
    scene_decode_failed,
    scene_limit_exceeded
};

struct SourceMissionLoadError {
    SourceMissionLoadErrorCode code{
        SourceMissionLoadErrorCode::archive_unavailable};
    std::filesystem::path path;
    std::string message;
};

struct SourceMissionLoadResult {
    std::string mission_id;
    std::filesystem::path archive_path;
    scene::RenderScene render_scene;
    std::size_t primitive_records{0};
    std::size_t rejected_models{0};
    std::size_t rejected_objects{0};
    std::size_t declared_scene_objects{0};
    std::size_t decoded_placements{0};
    std::size_t active_placements{0};
    std::size_t inactive_placements{0};
    std::size_t missing_placements{0};
};

class ISourceMissionLoader {
public:
    virtual ~ISourceMissionLoader() = default;

    [[nodiscard]] virtual core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>
    load(
        const std::filesystem::path& game_path,
        std::string_view mission_id) const = 0;
};

class ReadOnlySourceMissionLoader final : public ISourceMissionLoader {
public:
    [[nodiscard]] core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>
    load(
        const std::filesystem::path& game_path,
        std::string_view mission_id) const override;
};

[[nodiscard]] bool is_valid_source_mission_id(
    std::string_view mission_id) noexcept;

}

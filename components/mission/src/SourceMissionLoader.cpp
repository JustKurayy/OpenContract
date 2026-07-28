#include <contract/mission/SourceMissionLoader.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ZipArchive.hpp>

#include <cctype>
#include <cstdint>
#include <limits>
#include <utility>

namespace contract::mission {
namespace {

core::Result<SourceMissionLoadResult, SourceMissionLoadError> failure(
    SourceMissionLoadErrorCode code,
    std::filesystem::path path,
    std::string message) {
    return core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>::failure(
        {code, std::move(path), std::move(message)});
}

}

bool is_valid_source_mission_id(std::string_view mission_id) noexcept {
    if (mission_id.empty() || mission_id.size() > 32) {
        return false;
    }
    for (const unsigned char character : mission_id) {
        if (std::isalnum(character) == 0 &&
            character != '_' &&
            character != '-') {
            return false;
        }
    }
    return true;
}

core::Result<SourceMissionLoadResult, SourceMissionLoadError>
ReadOnlySourceMissionLoader::load(
    const std::filesystem::path& game_path,
    std::string_view mission_id) const {
    if (!is_valid_source_mission_id(mission_id)) {
        return failure(
            SourceMissionLoadErrorCode::invalid_identifier,
            {},
            "Source mission identifier contains unsupported characters");
    }

    const std::string identifier(mission_id);
    const auto archive_path =
        game_path /
        "Scenes" /
        identifier /
        (identifier + "_main.ZIP");
    auto archive_source = datasource::FileDataSource::open(archive_path);
    if (!archive_source.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_unavailable,
            archive_path,
            archive_source.error().message);
    }

    datasource::ReadBudget archive_budget(
        64U * 1024U * 1024U,
        1024U * 1024U);
    auto archive = formats::ZipArchiveIndex::read(
        archive_source.value(),
        archive_budget);
    if (!archive.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            archive.error().message);
    }

    const auto primitive_name =
        "Scenes/" + identifier + "/" + identifier + "_main.PRM";
    const auto* primitive_entry = archive.value().find(primitive_name);
    if (primitive_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::primitive_entry_missing,
            archive_path,
            "Source mission archive does not contain its primitive entry");
    }
    auto primitive_bytes = archive.value().read_entry(
        archive_source.value(),
        *primitive_entry,
        archive_budget,
        256U * 1024U * 1024U);
    if (!primitive_bytes.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            primitive_bytes.error().message);
    }

    datasource::MemoryDataSource primitive_source(primitive_bytes.value());
    datasource::ReadBudget primitive_budget(
        256U * 1024U * 1024U,
        1024U * 1024U);
    auto container = formats::PrimitiveContainerIndex::read(
        primitive_source,
        primitive_budget);
    if (!container.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::primitive_container_invalid,
            archive_path,
            container.error().message);
    }
    auto decoded = formats::PrimitiveSceneDecoder::decode(
        container.value(),
        primitive_source,
        primitive_budget);
    if (!decoded.has_value()) {
        return failure(
            decoded.error().code ==
                    formats::PrimitiveSceneDecodeErrorCode::limit_exceeded
                ? SourceMissionLoadErrorCode::scene_limit_exceeded
                : SourceMissionLoadErrorCode::scene_decode_failed,
            archive_path,
            decoded.error().message);
    }

    scene::RenderScene render_scene;
    render_scene.source_mesh_count = decoded.value().meshes.size();
    std::size_t vertex_count = 0;
    std::size_t index_count = 0;
    for (const auto& mesh : decoded.value().meshes) {
        vertex_count += mesh.positions.size();
        index_count += mesh.indices.size();
    }
    if (vertex_count > std::numeric_limits<std::uint32_t>::max()) {
        return failure(
            SourceMissionLoadErrorCode::scene_limit_exceeded,
            archive_path,
            "Source mission has too many vertices for the renderer");
    }
    render_scene.vertices.reserve(vertex_count);
    render_scene.indices.reserve(index_count);
    for (const auto& mesh : decoded.value().meshes) {
        const auto base_vertex =
            static_cast<std::uint32_t>(render_scene.vertices.size());
        for (const auto& position : mesh.positions) {
            render_scene.vertices.push_back(
                {position.x, position.y, position.z});
        }
        for (const auto index : mesh.indices) {
            if (index >
                std::numeric_limits<std::uint32_t>::max() - base_vertex) {
                return failure(
                    SourceMissionLoadErrorCode::scene_limit_exceeded,
                    archive_path,
                    "Source mission index would overflow");
            }
            render_scene.indices.push_back(base_vertex + index);
        }
    }

    return core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>::success(
        {
            identifier,
            archive_path,
            std::move(render_scene),
            container.value().records().size(),
            decoded.value().rejected_models,
            decoded.value().rejected_objects
        });
}

}

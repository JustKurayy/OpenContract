#include <contract/mission/SourceMissionLoader.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ScenePlacementDecoder.hpp>
#include <contract/formats/ZipArchive.hpp>
#include <contract/mission/SourceSceneBuilder.hpp>

#include <cctype>
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
    const auto property_name =
        "Scenes/" + identifier + "/" + identifier + "_main.PRP";
    const auto* primitive_entry = archive.value().find(primitive_name);
    if (primitive_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::primitive_entry_missing,
            archive_path,
            "Source mission archive does not contain its primitive entry");
    }
    const auto* property_entry = archive.value().find(property_name);
    if (property_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::property_entry_missing,
            archive_path,
            "Source mission archive does not contain its property entry");
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
    auto property_bytes = archive.value().read_entry(
        archive_source.value(),
        *property_entry,
        archive_budget,
        256U * 1024U * 1024U);
    if (!property_bytes.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            property_bytes.error().message);
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

    auto placements = formats::ScenePlacementDecoder::decode(
        property_bytes.value());
    if (!placements.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::property_decode_failed,
            archive_path,
            placements.error().message);
    }
    auto placed_scene = SourceSceneBuilder::build(
        decoded.value().meshes,
        placements.value().placements);
    if (!placed_scene.has_value()) {
        return failure(
            placed_scene.error().code ==
                    SourceSceneBuildErrorCode::scene_limit_exceeded
                ? SourceMissionLoadErrorCode::scene_limit_exceeded
                : SourceMissionLoadErrorCode::scene_decode_failed,
            archive_path,
            placed_scene.error().message);
    }

    return core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>::success(
        {
            identifier,
            archive_path,
            std::move(placed_scene.value().render_scene),
            container.value().records().size(),
            decoded.value().rejected_models,
            decoded.value().rejected_objects,
            placements.value().declared_objects,
            placements.value().placements.size(),
            placed_scene.value().active_placements,
            placed_scene.value().inactive_placements,
            placed_scene.value().missing_placements
        });
}

}

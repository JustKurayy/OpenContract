#include <contract/mission/SourceMissionLoader.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/GmsSceneDecoder.hpp>
#include <contract/formats/MaterialDatabaseDecoder.hpp>
#include <contract/formats/PrimitiveContainer.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ScenePlacementDecoder.hpp>
#include <contract/formats/TextureDatabaseDecoder.hpp>
#include <contract/formats/ZipArchive.hpp>
#include <contract/mission/SourceSceneBuilder.hpp>

#include <array>
#include <cctype>
#include <string_view>
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
        384U * 1024U * 1024U,
        2U * 1024U * 1024U);
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
    const auto hierarchy_name =
        "Scenes/" + identifier + "/" + identifier + "_main.GMS";
    const auto name_buffer_name =
        "Scenes/" + identifier + "/" + identifier + "_main.BUF";
    const auto material_name =
        "Scenes/" + identifier + "/" + identifier + "_main.MAT";
    const auto texture_name =
        "Scenes/" + identifier + "/" + identifier + "_main.TEX";
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
    const auto* hierarchy_entry = archive.value().find(hierarchy_name);
    if (hierarchy_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::hierarchy_entry_missing,
            archive_path,
            "Source mission archive does not contain its hierarchy entry");
    }
    const auto* name_buffer_entry =
        archive.value().find(name_buffer_name);
    if (name_buffer_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::name_buffer_entry_missing,
            archive_path,
            "Source mission archive does not contain its name buffer entry");
    }
    const auto* material_entry = archive.value().find(material_name);
    if (material_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::material_entry_missing,
            archive_path,
            "Source mission archive does not contain its material entry");
    }
    const auto* texture_entry = archive.value().find(texture_name);
    if (texture_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::texture_entry_missing,
            archive_path,
            "Source mission archive does not contain its texture entry");
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
    auto hierarchy_bytes = archive.value().read_entry(
        archive_source.value(),
        *hierarchy_entry,
        archive_budget,
        64U * 1024U * 1024U);
    if (!hierarchy_bytes.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            hierarchy_bytes.error().message);
    }
    auto name_buffer_bytes = archive.value().read_entry(
        archive_source.value(),
        *name_buffer_entry,
        archive_budget,
        64U * 1024U * 1024U);
    if (!name_buffer_bytes.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            name_buffer_bytes.error().message);
    }
    auto material_bytes = archive.value().read_entry(
        archive_source.value(),
        *material_entry,
        archive_budget,
        64U * 1024U * 1024U);
    if (!material_bytes.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            material_bytes.error().message);
    }
    auto texture_bytes = archive.value().read_entry(
        archive_source.value(),
        *texture_entry,
        archive_budget,
        256U * 1024U * 1024U);
    if (!texture_bytes.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            texture_bytes.error().message);
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
    auto hierarchy = formats::GmsSceneDecoder::decode(
        hierarchy_bytes.value(),
        name_buffer_bytes.value());
    if (!hierarchy.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::hierarchy_decode_failed,
            archive_path,
            hierarchy.error().message);
    }
    auto materials = formats::MaterialDatabaseDecoder::decode(
        material_bytes.value());
    if (!materials.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::material_decode_failed,
            archive_path,
            materials.error().message);
    }
    auto textures = formats::TextureDatabaseDecoder::decode(
        texture_bytes.value());
    if (!textures.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::texture_decode_failed,
            archive_path,
            textures.error().message);
    }
    const SourceSceneBuildResources resources{
        &materials.value(),
        &textures.value(),
        texture_bytes.value()
    };
    constexpr std::array<std::string_view, 1>
        preferred_spawn_nodes{
        "Pos_Hero"};
    constexpr std::array<std::string_view, 1>
        preferred_character_nodes{
        "Hero"};
    auto placed_scene = SourceSceneBuilder::build(
        decoded.value().meshes,
        placements.value().placements,
        hierarchy.value().nodes,
        resources,
        {
            preferred_spawn_nodes,
            preferred_character_nodes
        });
    if (!placed_scene.has_value()) {
        return failure(
            placed_scene.error().code ==
                    SourceSceneBuildErrorCode::scene_limit_exceeded
                ? SourceMissionLoadErrorCode::scene_limit_exceeded
                : SourceMissionLoadErrorCode::scene_decode_failed,
            archive_path,
            placed_scene.error().message);
    }

    const auto texture_count =
        placed_scene.value().render_scene.textures.size();
    const auto batch_count =
        placed_scene.value().render_scene.batches.size();
    SourceMissionLoadResult result;
    result.mission_id = identifier;
    result.archive_path = archive_path;
    result.render_scene =
        std::move(placed_scene.value().render_scene);
    result.collision_scene =
        std::move(placed_scene.value().collision_scene);
    result.player_model =
        std::move(placed_scene.value().player_model);
    result.primitive_records = container.value().records().size();
    result.rejected_models = decoded.value().rejected_models;
    result.rejected_objects = decoded.value().rejected_objects;
    result.declared_scene_objects =
        placements.value().declared_objects;
    result.decoded_placements =
        placements.value().placements.size();
    result.active_placements =
        placed_scene.value().active_placements;
    result.inactive_placements =
        placed_scene.value().inactive_placements;
    result.inherited_inactive_placements =
        placed_scene.value().inherited_inactive_placements;
    result.invisible_placements =
        placed_scene.value().invisible_placements;
    result.missing_placements =
        placed_scene.value().missing_placements;
    result.visibility_group_count =
        placed_scene.value().visibility_group_count;
    result.collision_meshes =
        placed_scene.value().collision_meshes;
    result.walkable_render_triangles =
        placed_scene.value().walkable_render_triangles;
    result.overlay_meshes =
        placed_scene.value().overlay_meshes;
    result.texture_count = texture_count;
    result.render_batch_count = batch_count;
    result.preferred_spawn =
        placed_scene.value().preferred_spawn;
    return core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>::success(
        std::move(result));
}

}

#include <contract/mission/SourceMissionLoader.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/GmsSceneDecoder.hpp>
#include <contract/formats/MaterialDatabaseDecoder.hpp>
#include <contract/formats/PrimitiveContainer.hpp>
#include <contract/formats/PrimitiveRigDecoder.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ScenePlacementDecoder.hpp>
#include <contract/formats/TextureDatabaseDecoder.hpp>
#include <contract/formats/ZipArchive.hpp>
#include <contract/mission/SourceSceneBuilder.hpp>

#include <array>
#include <cctype>
#include <cmath>
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

scene::RenderJointRole source_joint_role(
    std::string_view name) {
    if (name == "GROUND") {
        return scene::RenderJointRole::root;
    }
    if (name == "PELVIS") {
        return scene::RenderJointRole::pelvis;
    }
    if (name == "SPINE") {
        return scene::RenderJointRole::spine_lower;
    }
    if (name == "SPINE_1") {
        return scene::RenderJointRole::spine_middle;
    }
    if (name == "SPINE_2") {
        return scene::RenderJointRole::spine_upper;
    }
    if (name == "NECK") {
        return scene::RenderJointRole::neck;
    }
    if (name == "HEAD") {
        return scene::RenderJointRole::head;
    }
    if (name == "LEFT_CLAVICLE") {
        return scene::RenderJointRole::left_clavicle;
    }
    if (name == "LEFT_UPPER_ARM") {
        return scene::RenderJointRole::left_upper_arm;
    }
    if (name == "LEFT_FOREARM") {
        return scene::RenderJointRole::left_forearm;
    }
    if (name == "LEFT_HAND") {
        return scene::RenderJointRole::left_hand;
    }
    if (name == "RIGHT_CLAVICLE") {
        return scene::RenderJointRole::right_clavicle;
    }
    if (name == "RIGHT_UPPER_ARM") {
        return scene::RenderJointRole::right_upper_arm;
    }
    if (name == "RIGHT_FOREARM") {
        return scene::RenderJointRole::right_forearm;
    }
    if (name == "RIGHT_HAND") {
        return scene::RenderJointRole::right_hand;
    }
    if (name == "LEFT_THIGH") {
        return scene::RenderJointRole::left_thigh;
    }
    if (name == "LEFT_CALF") {
        return scene::RenderJointRole::left_calf;
    }
    if (name == "LEFT_FOOT") {
        return scene::RenderJointRole::left_foot;
    }
    if (name == "LEFT_TOE") {
        return scene::RenderJointRole::left_toe;
    }
    if (name == "RIGHT_THIGH") {
        return scene::RenderJointRole::right_thigh;
    }
    if (name == "RIGHT_CALF") {
        return scene::RenderJointRole::right_calf;
    }
    if (name == "RIGHT_FOOT") {
        return scene::RenderJointRole::right_foot;
    }
    if (name == "RIGHT_TOE") {
        return scene::RenderJointRole::right_toe;
    }
    return scene::RenderJointRole::unknown;
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
    constexpr std::array<std::string_view, 1>
        suppressed_dynamic_nodes{
        "HitmanCloth_00"};
    auto placed_scene = SourceSceneBuilder::build(
        decoded.value().meshes,
        placements.value().placements,
        hierarchy.value().nodes,
        resources,
        {
            preferred_spawn_nodes,
            preferred_character_nodes,
            suppressed_dynamic_nodes
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

    std::size_t rig_bone_count = 0;
    std::size_t skinned_vertex_count = 0;
    if (placed_scene.value().player_model.has_value() &&
        placed_scene.value().player_model_record.has_value() &&
        !placed_scene.value().player_model->skinning.empty()) {
        auto rig = formats::PrimitiveRigDecoder::decode(
            container.value(),
            primitive_source,
            placed_scene.value().player_model_record.value(),
            primitive_budget);
        if (!rig.has_value()) {
            return failure(
                SourceMissionLoadErrorCode::rig_decode_failed,
                archive_path,
                rig.error().message);
        }
        scene::RenderSkeleton skeleton;
        skeleton.joints.reserve(rig.value().bones.size());
        for (const auto& bone : rig.value().bones) {
            skeleton.joints.push_back(
                {
                    bone.name,
                    bone.parent_index,
                    source_joint_role(bone.name),
                    bone.reference_position
                });
        }
        for (const auto& skinning :
             placed_scene.value().player_model->skinning) {
            float total_weight = 0.0F;
            for (std::size_t influence = 0;
                 influence < skinning.weights.size();
                 ++influence) {
                const auto weight =
                    skinning.weights[influence];
                if (!std::isfinite(weight) ||
                    weight < 0.0F ||
                    weight > 1.0F ||
                    (weight > 0.0F &&
                     skinning.joints[influence] >=
                         skeleton.joints.size())) {
                    return failure(
                        SourceMissionLoadErrorCode::
                            rig_decode_failed,
                        archive_path,
                        "Source player skinning references an "
                        "invalid joint or weight");
                }
                total_weight += weight;
            }
            if (!std::isfinite(total_weight) ||
                std::abs(total_weight - 1.0F) > 0.001F) {
                return failure(
                    SourceMissionLoadErrorCode::
                        rig_decode_failed,
                    archive_path,
                    "Source player skinning weights are not normalized");
            }
        }
        rig_bone_count = skeleton.joints.size();
        skinned_vertex_count =
            placed_scene.value().player_model->skinning.size();
        placed_scene.value().player_model->skeleton =
            std::move(skeleton);
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
    result.suppressed_dynamic_placements =
        placed_scene.value().suppressed_dynamic_placements;
    result.texture_count = texture_count;
    result.render_batch_count = batch_count;
    result.rig_bone_count = rig_bone_count;
    result.skinned_vertex_count = skinned_vertex_count;
    result.preferred_spawn =
        placed_scene.value().preferred_spawn;
    return core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>::success(
        std::move(result));
}

}

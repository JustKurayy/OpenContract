#include <contract/mission/SourceMissionLoader.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/AnimationDatabaseDecoder.hpp>
#include <contract/formats/AnimationTrackDirectory.hpp>
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

core::Result<
    SourceCharacterAnimationClip,
    formats::AnimationDatabaseDecodeError>
source_animation_clip(
    std::string role,
    std::string path,
    const formats::AnimationClipDescriptor& descriptor,
    const formats::AnimationDatabaseIndex& index,
    const datasource::IReadOnlyDataSource& source,
    datasource::ReadBudget& budget) {
    auto encoded = index.read_encoded_clip(
        source,
        descriptor,
        budget);
    if (!encoded.has_value()) {
        return core::Result<
            SourceCharacterAnimationClip,
            formats::AnimationDatabaseDecodeError>::failure(
            encoded.error());
    }
    auto directories =
        formats::decode_animation_track_directories(
            encoded.value(),
            descriptor);
    if (!directories.has_value()) {
        return core::Result<
            SourceCharacterAnimationClip,
            formats::AnimationDatabaseDecodeError>::failure(
            directories.error());
    }
    std::vector<SourceCharacterAnimationChannel> channels;
    channels.reserve(directories.value().size());
    for (const auto& directory : directories.value()) {
        channels.push_back(
            {
                directory.channel_slot,
                directory.tracks.size()
            });
    }
    return core::Result<
        SourceCharacterAnimationClip,
        formats::AnimationDatabaseDecodeError>::success(
        {
            std::move(role),
            std::move(path),
            descriptor.index,
            descriptor.sample_count,
            descriptor.samples_per_second,
            descriptor.track_count,
            descriptor.encoded_size,
            std::move(channels)
        });
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
    const auto animation_name =
        "Scenes/" + identifier + "/" + identifier + "_main.ANM";
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
    const auto* animation_entry = archive.value().find(animation_name);
    if (animation_entry == nullptr) {
        return failure(
            SourceMissionLoadErrorCode::animation_entry_missing,
            archive_path,
            "Source mission archive does not contain its animation entry");
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
    auto animation_bytes = archive.value().read_entry(
        archive_source.value(),
        *animation_entry,
        archive_budget,
        16U * 1024U * 1024U);
    if (!animation_bytes.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::archive_invalid,
            archive_path,
            animation_bytes.error().message);
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
    datasource::MemoryDataSource animation_source(
        animation_bytes.value());
    datasource::ReadBudget animation_budget(
        2U * 1024U * 1024U,
        128U * 1024U);
    auto animations = formats::AnimationDatabaseIndex::read(
        animation_source,
        animation_budget);
    if (!animations.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::animation_decode_failed,
            archive_path,
            animations.error().message);
    }
    const auto animation_database =
        "anmcol:animationdatabase#" + identifier + "_Hitman";
    constexpr std::string_view idle_animation =
        "/Movement/Ambient/Stand_Relaxed";
    constexpr std::string_view walk_animation =
        "/Movement/Walk/Forward";
    constexpr std::string_view sprint_animation =
        "/Movement/Run/Forward";
    const auto idle_clip = animations.value().resolve(
        animation_database,
        idle_animation);
    const auto walk_clip = animations.value().resolve(
        animation_database,
        walk_animation);
    const auto sprint_clip = animations.value().resolve(
        animation_database,
        sprint_animation);
    if (!idle_clip.has_value() ||
        !walk_clip.has_value() ||
        !sprint_clip.has_value()) {
        const auto message = !idle_clip.has_value()
            ? idle_clip.error().message
            : !walk_clip.has_value()
                ? walk_clip.error().message
                : sprint_clip.error().message;
        return failure(
            SourceMissionLoadErrorCode::animation_decode_failed,
            archive_path,
            "Source character animation selection failed: " + message);
    }
    auto idle = source_animation_clip(
        "idle",
        std::string(idle_animation),
        idle_clip.value(),
        animations.value(),
        animation_source,
        animation_budget);
    if (!idle.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::animation_decode_failed,
            archive_path,
            "Source character idle routing failed: " +
                idle.error().message);
    }
    auto walk = source_animation_clip(
        "walk",
        std::string(walk_animation),
        walk_clip.value(),
        animations.value(),
        animation_source,
        animation_budget);
    if (!walk.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::animation_decode_failed,
            archive_path,
            "Source character walk routing failed: " +
                walk.error().message);
    }
    auto sprint = source_animation_clip(
        "sprint",
        std::string(sprint_animation),
        sprint_clip.value(),
        animations.value(),
        animation_source,
        animation_budget);
    if (!sprint.has_value()) {
        return failure(
            SourceMissionLoadErrorCode::animation_decode_failed,
            archive_path,
            "Source character sprint routing failed: " +
                sprint.error().message);
    }
    SourceCharacterAnimations character_animations{
        animation_database,
        std::move(idle.value()),
        std::move(walk.value()),
        std::move(sprint.value())
    };
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
    result.animation_path_count = animations.value().paths().size();
    result.animation_database_count =
        animations.value().databases().size();
    result.animation_clip_count = animations.value().clips().size();
    result.character_animations = std::move(character_animations);
    result.preferred_spawn =
        placed_scene.value().preferred_spawn;
    return core::Result<
        SourceMissionLoadResult,
        SourceMissionLoadError>::success(
        std::move(result));
}

}

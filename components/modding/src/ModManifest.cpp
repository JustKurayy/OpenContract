#include <contract/modding/ModManifest.hpp>

#include <contract/datasource/DataSource.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace contract::modding {
namespace {

using Json = nlohmann::json;

class SyntaxErrorObserver final : public nlohmann::json_sax<Json> {
public:
    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool string(string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override { return true; }
    bool key(string_t&) override { return true; }
    bool end_object() override { return true; }
    bool start_array(std::size_t) override { return true; }
    bool end_array() override { return true; }

    bool parse_error(
        std::size_t position,
        const std::string&,
        const nlohmann::detail::exception&) override {
        byte_offset = position;
        return false;
    }

    std::optional<std::size_t> byte_offset;
};

std::optional<std::size_t> find_syntax_error_offset(std::string_view input) {
    SyntaxErrorObserver observer;
    static_cast<void>(Json::sax_parse(input, &observer));
    return observer.byte_offset;
}

ManifestError schema_error(std::string path, std::string expectation) {
    return {
        ManifestErrorCode::schema_error,
        std::move(path) + " " + std::move(expectation),
        std::nullopt};
}

const Json* required_member(
    const Json& object,
    std::string_view key,
    std::string path,
    ManifestError& error) {
    if (!object.is_object()) {
        error = schema_error(std::move(path), "must be an object");
        return nullptr;
    }
    const auto iterator = object.find(std::string(key));
    if (iterator == object.end()) {
        error = schema_error(
            std::move(path),
            "must contain '" + std::string(key) + "'");
        return nullptr;
    }
    return &*iterator;
}

bool read_string(
    const Json& object,
    std::string_view key,
    std::string path,
    std::string& output,
    ManifestError& error) {
    const Json* value = required_member(object, key, path, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_string()) {
        error = schema_error(
            std::move(path) + "." + std::string(key),
            "must be a string");
        return false;
    }
    output = value->get_ref<const std::string&>();
    return true;
}

bool read_u32(
    const Json& object,
    std::string_view key,
    std::string path,
    std::uint32_t& output,
    ManifestError& error) {
    const Json* value = required_member(object, key, path, error);
    if (value == nullptr) {
        return false;
    }
    const auto* number = value->get_ptr<const Json::number_unsigned_t*>();
    if (number == nullptr || *number > std::numeric_limits<std::uint32_t>::max()) {
        error = schema_error(
            std::move(path) + "." + std::string(key),
            "must be an unsigned 32-bit integer");
        return false;
    }
    output = static_cast<std::uint32_t>(*number);
    return true;
}

bool read_number(
    const Json& value,
    std::string path,
    float& output,
    ManifestError& error) {
    double number = 0.0;
    if (const auto* floating = value.get_ptr<const Json::number_float_t*>()) {
        number = *floating;
    } else if (const auto* signed_value = value.get_ptr<const Json::number_integer_t*>()) {
        number = static_cast<double>(*signed_value);
    } else if (const auto* unsigned_value =
                   value.get_ptr<const Json::number_unsigned_t*>()) {
        number = static_cast<double>(*unsigned_value);
    } else {
        error = schema_error(std::move(path), "must be numeric");
        return false;
    }

    if (!std::isfinite(number) ||
        number < static_cast<double>(std::numeric_limits<float>::lowest()) ||
        number > static_cast<double>(std::numeric_limits<float>::max())) {
        error = schema_error(std::move(path), "must be a finite 32-bit float");
        return false;
    }
    output = static_cast<float>(number);
    return true;
}

template <std::size_t Size>
bool read_float_array(
    const Json& value,
    std::string path,
    std::array<float, Size>& output,
    ManifestError& error) {
    if (!value.is_array() || value.size() != Size) {
        error = schema_error(
            std::move(path),
            "must be an array of " + std::to_string(Size) + " numbers");
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index) {
        if (!read_number(
                value[index],
                path + "[" + std::to_string(index) + "]",
                output[index],
                error)) {
            return false;
        }
    }
    return true;
}

bool read_version(
    const Json& value,
    std::string path,
    SemanticVersion& output,
    ManifestError& error) {
    return read_u32(value, "major", path, output.major, error) &&
           read_u32(value, "minor", path, output.minor, error) &&
           read_u32(value, "patch", std::move(path), output.patch, error);
}

template <typename Identifier>
bool read_identifier_array(
    const Json& value,
    std::string path,
    std::vector<Identifier>& output,
    ManifestError& error) {
    if (!value.is_array()) {
        error = schema_error(std::move(path), "must be an array");
        return false;
    }
    output.clear();
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!value[index].is_string()) {
            error = schema_error(
                path + "[" + std::to_string(index) + "]",
                "must be a string identifier");
            return false;
        }
        output.emplace_back(value[index].get_ref<const std::string&>());
    }
    return true;
}

Json write_version(const SemanticVersion& version) {
    return {
        {"major", version.major},
        {"minor", version.minor},
        {"patch", version.patch}};
}

Json write_transform(const scene::Transform& transform) {
    return {
        {"position", transform.position},
        {"rotation", transform.rotation},
        {"scale", transform.scale}};
}

Json serialize_package(const ModPackage& package) {
    Json root;
    root["schema_version"] = mod_manifest_schema_version;
    root["package"] = {
        {"id", package.id.value()},
        {"version", write_version(package.version)},
        {"metadata",
         {
             {"name", package.metadata.name},
             {"author", package.metadata.author},
             {"description", package.metadata.description}
         }}};

    root["dependencies"] = Json::array();
    for (const auto& dependency : package.dependencies) {
        root["dependencies"].push_back(
            {
                {"package", dependency.package.value()},
                {"minimum_version", write_version(dependency.minimum_version)}
            });
    }

    root["assets"] = Json::array();
    for (const auto& asset : package.assets) {
        root["assets"].push_back(
            {
                {"id", asset.id.value()},
                {"source", asset.source.generic_string()}
            });
    }

    root["navigation_graphs"] = Json::array();
    for (const auto& graph : package.navigation_graphs) {
        Json nodes = Json::array();
        for (const auto& node : graph.nodes) {
            Json neighbors = Json::array();
            for (const auto& neighbor : node.neighbors) {
                neighbors.push_back(neighbor.value());
            }
            nodes.push_back(
                {
                    {"id", node.id.value()},
                    {"position", node.position},
                    {"neighbors", std::move(neighbors)}
                });
        }
        root["navigation_graphs"].push_back(
            {
                {"id", graph.id.value()},
                {"nodes", std::move(nodes)}
            });
    }

    root["maps"] = Json::array();
    for (const auto& map : package.maps) {
        Json entities = Json::array();
        for (const auto& entity : map.entities) {
            Json components = Json::array();
            for (const auto& component : entity.components) {
                Json asset_ids = Json::array();
                for (const auto& asset : component.assets) {
                    asset_ids.push_back(asset.value());
                }
                components.push_back(
                    {
                        {"type", component.type},
                        {"assets", std::move(asset_ids)}
                    });
            }
            entities.push_back(
                {
                    {"id", entity.id.value()},
                    {"transform", write_transform(entity.transform)},
                    {"components", std::move(components)}
                });
        }
        Json navigation = nullptr;
        if (map.navigation.has_value()) {
            navigation = map.navigation->value();
        }
        root["maps"].push_back(
            {
                {"id", map.id.value()},
                {"navigation", std::move(navigation)},
                {"entities", std::move(entities)}
            });
    }

    root["missions"] = Json::array();
    for (const auto& mission : package.missions) {
        Json objectives = Json::array();
        for (const auto& objective : mission.objectives) {
            Json targets = Json::array();
            for (const auto& target : objective.target_references) {
                targets.push_back(target.value());
            }
            Json spawns = Json::array();
            for (const auto& spawn : objective.spawn_references) {
                spawns.push_back(spawn.value());
            }
            objectives.push_back(
                {
                    {"id", objective.id.value()},
                    {"targets", std::move(targets)},
                    {"spawns", std::move(spawns)}
                });
        }
        root["missions"].push_back(
            {
                {"id", mission.id.value()},
                {"map", mission.map.value()},
                {"objectives", std::move(objectives)}
            });
    }
    return root;
}

bool parse_package_identity(
    const Json& root,
    ModPackage& package,
    ManifestError& error) {
    const Json* package_json = required_member(root, "package", "$", error);
    if (package_json == nullptr || !package_json->is_object()) {
        if (package_json != nullptr) {
            error = schema_error("$.package", "must be an object");
        }
        return false;
    }

    std::string package_id;
    if (!read_string(*package_json, "id", "$.package", package_id, error)) {
        return false;
    }
    package.id = ModPackageId(std::move(package_id));

    const Json* version = required_member(
        *package_json,
        "version",
        "$.package",
        error);
    if (version == nullptr ||
        !read_version(*version, "$.package.version", package.version, error)) {
        return false;
    }

    const Json* metadata = required_member(
        *package_json,
        "metadata",
        "$.package",
        error);
    if (metadata == nullptr || !metadata->is_object()) {
        if (metadata != nullptr) {
            error = schema_error("$.package.metadata", "must be an object");
        }
        return false;
    }
    return read_string(
               *metadata,
               "name",
               "$.package.metadata",
               package.metadata.name,
               error) &&
           read_string(
               *metadata,
               "author",
               "$.package.metadata",
               package.metadata.author,
               error) &&
           read_string(
               *metadata,
               "description",
               "$.package.metadata",
               package.metadata.description,
               error);
}

bool parse_dependencies(
    const Json& root,
    ModPackage& package,
    ManifestError& error) {
    const Json* values = required_member(root, "dependencies", "$", error);
    if (values == nullptr || !values->is_array()) {
        if (values != nullptr) {
            error = schema_error("$.dependencies", "must be an array");
        }
        return false;
    }
    package.dependencies.clear();
    package.dependencies.reserve(values->size());
    for (std::size_t index = 0; index < values->size(); ++index) {
        const auto path = "$.dependencies[" + std::to_string(index) + "]";
        const Json& value = (*values)[index];
        std::string id;
        PackageDependency dependency;
        if (!read_string(value, "package", path, id, error)) {
            return false;
        }
        dependency.package = ModPackageId(std::move(id));
        const Json* version = required_member(value, "minimum_version", path, error);
        if (version == nullptr ||
            !read_version(
                *version,
                path + ".minimum_version",
                dependency.minimum_version,
                error)) {
            return false;
        }
        package.dependencies.push_back(std::move(dependency));
    }
    return true;
}

bool parse_assets(
    const Json& root,
    ModPackage& package,
    ManifestError& error) {
    const Json* values = required_member(root, "assets", "$", error);
    if (values == nullptr || !values->is_array()) {
        if (values != nullptr) {
            error = schema_error("$.assets", "must be an array");
        }
        return false;
    }
    package.assets.clear();
    package.assets.reserve(values->size());
    for (std::size_t index = 0; index < values->size(); ++index) {
        const auto path = "$.assets[" + std::to_string(index) + "]";
        std::string id;
        std::string source;
        if (!read_string((*values)[index], "id", path, id, error) ||
            !read_string((*values)[index], "source", path, source, error)) {
            return false;
        }
        package.assets.push_back(
            {assets::AssetId(std::move(id)), std::filesystem::path(std::move(source))});
    }
    return true;
}

bool parse_navigation(
    const Json& root,
    ModPackage& package,
    ManifestError& error) {
    const Json* graphs = required_member(root, "navigation_graphs", "$", error);
    if (graphs == nullptr || !graphs->is_array()) {
        if (graphs != nullptr) {
            error = schema_error("$.navigation_graphs", "must be an array");
        }
        return false;
    }
    package.navigation_graphs.clear();
    package.navigation_graphs.reserve(graphs->size());
    for (std::size_t graph_index = 0; graph_index < graphs->size(); ++graph_index) {
        const auto graph_path =
            "$.navigation_graphs[" + std::to_string(graph_index) + "]";
        const Json& graph_json = (*graphs)[graph_index];
        std::string graph_id;
        if (!read_string(graph_json, "id", graph_path, graph_id, error)) {
            return false;
        }
        const Json* nodes = required_member(graph_json, "nodes", graph_path, error);
        if (nodes == nullptr || !nodes->is_array()) {
            if (nodes != nullptr) {
                error = schema_error(graph_path + ".nodes", "must be an array");
            }
            return false;
        }

        navigation::NavigationGraph graph;
        graph.id = navigation::NavigationGraphId(std::move(graph_id));
        graph.nodes.reserve(nodes->size());
        for (std::size_t node_index = 0; node_index < nodes->size(); ++node_index) {
            const auto node_path =
                graph_path + ".nodes[" + std::to_string(node_index) + "]";
            const Json& node_json = (*nodes)[node_index];
            std::string node_id;
            navigation::NavigationNode node;
            if (!read_string(node_json, "id", node_path, node_id, error)) {
                return false;
            }
            node.id = navigation::NavigationNodeId(std::move(node_id));
            const Json* position =
                required_member(node_json, "position", node_path, error);
            const Json* neighbors =
                required_member(node_json, "neighbors", node_path, error);
            if (position == nullptr || neighbors == nullptr ||
                !read_float_array(
                    *position,
                    node_path + ".position",
                    node.position,
                    error) ||
                !read_identifier_array(
                    *neighbors,
                    node_path + ".neighbors",
                    node.neighbors,
                    error)) {
                return false;
            }
            graph.nodes.push_back(std::move(node));
        }
        package.navigation_graphs.push_back(std::move(graph));
    }
    return true;
}

bool parse_transform(
    const Json& value,
    std::string path,
    scene::Transform& transform,
    ManifestError& error) {
    const Json* position = required_member(value, "position", path, error);
    const Json* rotation = required_member(value, "rotation", path, error);
    const Json* scale = required_member(value, "scale", path, error);
    return position != nullptr &&
           rotation != nullptr &&
           scale != nullptr &&
           read_float_array(
               *position,
               path + ".position",
               transform.position,
               error) &&
           read_float_array(
               *rotation,
               path + ".rotation",
               transform.rotation,
               error) &&
           read_float_array(
               *scale,
               path + ".scale",
               transform.scale,
               error);
}

bool parse_maps(
    const Json& root,
    ModPackage& package,
    ManifestError& error) {
    const Json* maps = required_member(root, "maps", "$", error);
    if (maps == nullptr || !maps->is_array()) {
        if (maps != nullptr) {
            error = schema_error("$.maps", "must be an array");
        }
        return false;
    }
    package.maps.clear();
    package.maps.reserve(maps->size());
    for (std::size_t map_index = 0; map_index < maps->size(); ++map_index) {
        const auto map_path = "$.maps[" + std::to_string(map_index) + "]";
        const Json& map_json = (*maps)[map_index];
        std::string map_id;
        if (!read_string(map_json, "id", map_path, map_id, error)) {
            return false;
        }
        scene::MapDefinition map;
        map.id = scene::MapId(std::move(map_id));

        const Json* navigation =
            required_member(map_json, "navigation", map_path, error);
        if (navigation == nullptr) {
            return false;
        }
        if (navigation->is_string()) {
            map.navigation = navigation::NavigationGraphId(
                navigation->get_ref<const std::string&>());
        } else if (!navigation->is_null()) {
            error = schema_error(
                map_path + ".navigation",
                "must be null or a string identifier");
            return false;
        }

        const Json* entities =
            required_member(map_json, "entities", map_path, error);
        if (entities == nullptr || !entities->is_array()) {
            if (entities != nullptr) {
                error = schema_error(map_path + ".entities", "must be an array");
            }
            return false;
        }
        map.entities.reserve(entities->size());
        for (std::size_t entity_index = 0;
             entity_index < entities->size();
             ++entity_index) {
            const auto entity_path =
                map_path + ".entities[" + std::to_string(entity_index) + "]";
            const Json& entity_json = (*entities)[entity_index];
            std::string entity_id;
            scene::EntityDefinition entity;
            if (!read_string(
                    entity_json,
                    "id",
                    entity_path,
                    entity_id,
                    error)) {
                return false;
            }
            entity.id = scene::EntityId(std::move(entity_id));
            const Json* transform =
                required_member(entity_json, "transform", entity_path, error);
            const Json* components =
                required_member(entity_json, "components", entity_path, error);
            if (transform == nullptr || components == nullptr ||
                !parse_transform(
                    *transform,
                    entity_path + ".transform",
                    entity.transform,
                    error)) {
                return false;
            }
            if (!components->is_array()) {
                error = schema_error(
                    entity_path + ".components",
                    "must be an array");
                return false;
            }
            entity.components.reserve(components->size());
            for (std::size_t component_index = 0;
                 component_index < components->size();
                 ++component_index) {
                const auto component_path =
                    entity_path + ".components[" +
                    std::to_string(component_index) + "]";
                const Json& component_json = (*components)[component_index];
                scene::ComponentReference component;
                if (!read_string(
                        component_json,
                        "type",
                        component_path,
                        component.type,
                        error)) {
                    return false;
                }
                const Json* asset_ids = required_member(
                    component_json,
                    "assets",
                    component_path,
                    error);
                if (asset_ids == nullptr ||
                    !read_identifier_array(
                        *asset_ids,
                        component_path + ".assets",
                        component.assets,
                        error)) {
                    return false;
                }
                entity.components.push_back(std::move(component));
            }
            map.entities.push_back(std::move(entity));
        }
        package.maps.push_back(std::move(map));
    }
    return true;
}

bool parse_missions(
    const Json& root,
    ModPackage& package,
    ManifestError& error) {
    const Json* missions = required_member(root, "missions", "$", error);
    if (missions == nullptr || !missions->is_array()) {
        if (missions != nullptr) {
            error = schema_error("$.missions", "must be an array");
        }
        return false;
    }
    package.missions.clear();
    package.missions.reserve(missions->size());
    for (std::size_t mission_index = 0;
         mission_index < missions->size();
         ++mission_index) {
        const auto mission_path =
            "$.missions[" + std::to_string(mission_index) + "]";
        const Json& mission_json = (*missions)[mission_index];
        std::string mission_id;
        std::string map_id;
        if (!read_string(
                mission_json,
                "id",
                mission_path,
                mission_id,
                error) ||
            !read_string(
                mission_json,
                "map",
                mission_path,
                map_id,
                error)) {
            return false;
        }

        mission::MissionDefinition mission;
        mission.id = mission::MissionId(std::move(mission_id));
        mission.map = scene::MapId(std::move(map_id));
        const Json* objectives =
            required_member(mission_json, "objectives", mission_path, error);
        if (objectives == nullptr || !objectives->is_array()) {
            if (objectives != nullptr) {
                error = schema_error(
                    mission_path + ".objectives",
                    "must be an array");
            }
            return false;
        }
        mission.objectives.reserve(objectives->size());
        for (std::size_t objective_index = 0;
             objective_index < objectives->size();
             ++objective_index) {
            const auto objective_path =
                mission_path + ".objectives[" +
                std::to_string(objective_index) + "]";
            const Json& objective_json = (*objectives)[objective_index];
            std::string objective_id;
            mission::MissionObjective objective;
            if (!read_string(
                    objective_json,
                    "id",
                    objective_path,
                    objective_id,
                    error)) {
                return false;
            }
            objective.id = mission::ObjectiveId(std::move(objective_id));
            const Json* targets =
                required_member(objective_json, "targets", objective_path, error);
            const Json* spawns =
                required_member(objective_json, "spawns", objective_path, error);
            if (targets == nullptr || spawns == nullptr ||
                !read_identifier_array(
                    *targets,
                    objective_path + ".targets",
                    objective.target_references,
                    error) ||
                !read_identifier_array(
                    *spawns,
                    objective_path + ".spawns",
                    objective.spawn_references,
                    error)) {
                return false;
            }
            mission.objectives.push_back(std::move(objective));
        }
        package.missions.push_back(std::move(mission));
    }
    return true;
}

}

core::Result<std::string, ManifestError> ModManifestCodec::serialize(
    const ModPackage& package) const {
    const ModPackageValidator validator;
    const auto issues = validator.validate(package);
    if (!issues.empty()) {
        return core::Result<std::string, ManifestError>::failure(
            {
                ManifestErrorCode::validation_error,
                issues.front().message,
                std::nullopt
            });
    }
    return core::Result<std::string, ManifestError>::success(
        serialize_package(package).dump());
}

core::Result<ModPackage, ManifestError> ModManifestCodec::parse(
    std::string_view input,
    std::size_t maximum_size) const {
    if (input.size() > maximum_size) {
        return core::Result<ModPackage, ManifestError>::failure(
            {
                ManifestErrorCode::size_limit_exceeded,
                "Manifest exceeds the configured size limit",
                std::nullopt
            });
    }

    Json root = Json::parse(input, nullptr, false);
    if (root.is_discarded()) {
        return core::Result<ModPackage, ManifestError>::failure(
            {
                ManifestErrorCode::syntax_error,
                "Manifest is not valid JSON",
                find_syntax_error_offset(input)
            });
    }
    if (!root.is_object()) {
        return core::Result<ModPackage, ManifestError>::failure(
            schema_error("$", "must be an object"));
    }

    ManifestError error;
    std::uint32_t schema_version = 0;
    if (!read_u32(
            root,
            "schema_version",
            "$",
            schema_version,
            error)) {
        return core::Result<ModPackage, ManifestError>::failure(std::move(error));
    }
    if (schema_version != mod_manifest_schema_version) {
        return core::Result<ModPackage, ManifestError>::failure(
            {
                ManifestErrorCode::unsupported_version,
                "Manifest schema version is not supported",
                std::nullopt
            });
    }

    ModPackage package;
    if (!parse_package_identity(root, package, error) ||
        !parse_dependencies(root, package, error) ||
        !parse_assets(root, package, error) ||
        !parse_navigation(root, package, error) ||
        !parse_maps(root, package, error) ||
        !parse_missions(root, package, error)) {
        return core::Result<ModPackage, ManifestError>::failure(std::move(error));
    }

    const ModPackageValidator validator;
    const auto issues = validator.validate(package);
    if (!issues.empty()) {
        return core::Result<ModPackage, ManifestError>::failure(
            {
                ManifestErrorCode::validation_error,
                issues.front().message,
                std::nullopt
            });
    }

    return core::Result<ModPackage, ManifestError>::success(std::move(package));
}

core::Result<ModPackage, ManifestError> ModManifestCodec::parse_file(
    const std::filesystem::path& path,
    std::size_t maximum_size) const {
    const auto source = datasource::FileDataSource::open(path);
    if (!source) {
        std::optional<std::size_t> offset;
        if (source.error().offset <= std::numeric_limits<std::size_t>::max()) {
            offset = static_cast<std::size_t>(source.error().offset);
        }
        return core::Result<ModPackage, ManifestError>::failure(
            {
                ManifestErrorCode::source_error,
                source.error().message,
                offset
            });
    }
    if (source.value().size() > maximum_size ||
        source.value().size() > std::numeric_limits<std::size_t>::max()) {
        return core::Result<ModPackage, ManifestError>::failure(
            {
                ManifestErrorCode::size_limit_exceeded,
                "Manifest exceeds the configured size limit",
                std::nullopt
            });
    }

    const auto length = static_cast<std::size_t>(source.value().size());
    datasource::ReadBudget budget(maximum_size, maximum_size);
    const auto bytes = source.value().read(0, length, budget);
    if (!bytes) {
        std::optional<std::size_t> offset;
        if (bytes.error().offset <= std::numeric_limits<std::size_t>::max()) {
            offset = static_cast<std::size_t>(bytes.error().offset);
        }
        return core::Result<ModPackage, ManifestError>::failure(
            {
                ManifestErrorCode::source_error,
                bytes.error().message,
                offset
            });
    }

    std::string input(bytes.value().size(), '\0');
    if (!bytes.value().empty()) {
        std::memcpy(input.data(), bytes.value().data(), bytes.value().size());
    }
    return parse(input, maximum_size);
}

}

#include <contract/navigation/Pathfinding.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <unordered_map>

namespace contract::navigation {
namespace {

double distance_between(
    const NavigationNode& left,
    const NavigationNode& right) {
    const double x =
        static_cast<double>(right.position[0]) -
        static_cast<double>(left.position[0]);
    const double y =
        static_cast<double>(right.position[1]) -
        static_cast<double>(left.position[1]);
    const double z =
        static_cast<double>(right.position[2]) -
        static_cast<double>(left.position[2]);
    return std::sqrt((x * x) + (y * y) + (z * z));
}

}

core::Result<NavigationPath, PathQueryError> NavigationPathfinder::shortest_path(
    const NavigationGraph& graph,
    const NavigationNodeId& start,
    const NavigationNodeId& goal) const {
    const NavigationValidator validator;
    const auto issues = validator.validate(graph);
    if (!issues.empty()) {
        return core::Result<NavigationPath, PathQueryError>::failure(
            {PathQueryErrorCode::invalid_graph, issues.front().message});
    }

    std::unordered_map<std::string, std::size_t> node_indices;
    node_indices.reserve(graph.nodes.size());
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        node_indices.emplace(graph.nodes[index].id.value(), index);
    }

    const auto start_match = node_indices.find(start.value());
    if (start_match == node_indices.end()) {
        return core::Result<NavigationPath, PathQueryError>::failure(
            {
                PathQueryErrorCode::start_not_found,
                "Navigation start node was not found: " + start.value()
            });
    }
    const auto goal_match = node_indices.find(goal.value());
    if (goal_match == node_indices.end()) {
        return core::Result<NavigationPath, PathQueryError>::failure(
            {
                PathQueryErrorCode::goal_not_found,
                "Navigation goal node was not found: " + goal.value()
            });
    }

    const auto start_index = start_match->second;
    const auto goal_index = goal_match->second;
    const auto infinity = std::numeric_limits<double>::infinity();
    std::vector<double> distances(graph.nodes.size(), infinity);
    std::vector<std::optional<std::size_t>> predecessors(graph.nodes.size());
    std::vector<bool> visited(graph.nodes.size(), false);
    distances[start_index] = 0.0;

    for (std::size_t iteration = 0; iteration < graph.nodes.size(); ++iteration) {
        std::optional<std::size_t> current;
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            if (visited[index] || !std::isfinite(distances[index])) {
                continue;
            }
            if (!current.has_value() ||
                distances[index] < distances[*current] ||
                (distances[index] == distances[*current] &&
                 graph.nodes[index].id.value() <
                     graph.nodes[*current].id.value())) {
                current = index;
            }
        }
        if (!current.has_value() || *current == goal_index) {
            break;
        }

        visited[*current] = true;
        const auto& current_node = graph.nodes[*current];
        for (const auto& neighbor_id : current_node.neighbors) {
            const auto neighbor_index = node_indices.at(neighbor_id.value());
            if (visited[neighbor_index]) {
                continue;
            }
            const double candidate =
                distances[*current] +
                distance_between(current_node, graph.nodes[neighbor_index]);
            if (!std::isfinite(candidate)) {
                return core::Result<NavigationPath, PathQueryError>::failure(
                    {
                        PathQueryErrorCode::invalid_graph,
                        "Navigation path cost is not finite"
                    });
            }

            const bool lower_cost = candidate < distances[neighbor_index];
            const bool deterministic_tie =
                candidate == distances[neighbor_index] &&
                (!predecessors[neighbor_index].has_value() ||
                 current_node.id.value() <
                     graph.nodes[*predecessors[neighbor_index]].id.value());
            if (lower_cost || deterministic_tie) {
                distances[neighbor_index] = candidate;
                predecessors[neighbor_index] = *current;
            }
        }
    }

    if (!std::isfinite(distances[goal_index])) {
        return core::Result<NavigationPath, PathQueryError>::failure(
            {
                PathQueryErrorCode::no_path,
                "No navigation path exists between the requested nodes"
            });
    }

    std::vector<NavigationNodeId> reversed;
    reversed.reserve(graph.nodes.size());
    auto cursor = goal_index;
    reversed.push_back(graph.nodes[cursor].id);
    while (cursor != start_index) {
        if (!predecessors[cursor].has_value()) {
            return core::Result<NavigationPath, PathQueryError>::failure(
                {
                    PathQueryErrorCode::no_path,
                    "No navigation path exists between the requested nodes"
                });
        }
        cursor = *predecessors[cursor];
        reversed.push_back(graph.nodes[cursor].id);
    }
    std::reverse(reversed.begin(), reversed.end());

    return core::Result<NavigationPath, PathQueryError>::success(
        {std::move(reversed), distances[goal_index]});
}

}

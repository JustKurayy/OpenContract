#pragma once

#include <contract/core/Result.hpp>
#include <contract/navigation/Navigation.hpp>

#include <string>
#include <vector>

namespace contract::navigation {

enum class PathQueryErrorCode {
    invalid_graph,
    start_not_found,
    goal_not_found,
    no_path
};

struct PathQueryError {
    PathQueryErrorCode code{PathQueryErrorCode::invalid_graph};
    std::string message;
};

struct NavigationPath {
    std::vector<NavigationNodeId> nodes;
    double total_cost{0.0};
};

class NavigationPathfinder {
public:
    [[nodiscard]] core::Result<NavigationPath, PathQueryError> shortest_path(
        const NavigationGraph& graph,
        const NavigationNodeId& start,
        const NavigationNodeId& goal) const;
};

}

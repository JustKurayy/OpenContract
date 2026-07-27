#pragma once

#include <contract/core/Identifier.hpp>

#include <array>
#include <string>
#include <vector>

namespace contract::navigation {

using NavigationGraphId = core::Identifier<struct NavigationGraphIdTag>;
using NavigationNodeId = core::Identifier<struct NavigationNodeIdTag>;

struct NavigationNode {
    NavigationNodeId id;
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::vector<NavigationNodeId> neighbors;
};

struct NavigationGraph {
    NavigationGraphId id;
    std::vector<NavigationNode> nodes;
};

enum class NavigationIssueCode {
    invalid_graph_identifier,
    invalid_node_identifier,
    duplicate_node_identifier,
    invalid_position,
    missing_neighbor_reference
};

struct NavigationIssue {
    NavigationIssueCode code{NavigationIssueCode::invalid_graph_identifier};
    std::string message;
};

class NavigationValidator {
public:
    [[nodiscard]] std::vector<NavigationIssue> validate(
        const NavigationGraph& graph) const;
};

}

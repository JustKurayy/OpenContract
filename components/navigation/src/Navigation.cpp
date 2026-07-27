#include <contract/navigation/Navigation.hpp>

#include <cmath>
#include <unordered_set>

namespace contract::navigation {

std::vector<NavigationIssue> NavigationValidator::validate(
    const NavigationGraph& graph) const {
    std::vector<NavigationIssue> issues;
    if (!graph.id.valid()) {
        issues.push_back(
            {NavigationIssueCode::invalid_graph_identifier,
             "Navigation graph identifier is empty"});
    }

    std::unordered_set<std::string> node_identifiers;
    for (const auto& node : graph.nodes) {
        if (!node.id.valid()) {
            issues.push_back(
                {NavigationIssueCode::invalid_node_identifier,
                 "Navigation node identifier is empty"});
        } else if (!node_identifiers.insert(node.id.value()).second) {
            issues.push_back(
                {NavigationIssueCode::duplicate_node_identifier,
                 "Duplicate navigation node identifier: " + node.id.value()});
        }

        bool finite_position = true;
        for (const auto coordinate : node.position) {
            finite_position = finite_position && std::isfinite(coordinate);
        }
        if (!finite_position) {
            issues.push_back(
                {NavigationIssueCode::invalid_position,
                 "Navigation node position contains a non-finite value"});
        }
    }

    for (const auto& node : graph.nodes) {
        for (const auto& neighbor : node.neighbors) {
            if (!neighbor.valid() || !node_identifiers.contains(neighbor.value())) {
                issues.push_back(
                    {NavigationIssueCode::missing_neighbor_reference,
                     "Navigation node references an undeclared neighbor: " +
                         neighbor.value()});
            }
        }
    }

    return issues;
}

}

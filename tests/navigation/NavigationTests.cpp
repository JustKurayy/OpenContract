#include "TestSupport.hpp"

#include <contract/navigation/Navigation.hpp>

#include <limits>
#include <vector>

namespace {

bool contains_issue(
    const std::vector<contract::navigation::NavigationIssue>& issues,
    contract::navigation::NavigationIssueCode code) {
    for (const auto& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

contract::navigation::NavigationGraph valid_graph() {
    using namespace contract::navigation;

    return {
        NavigationGraphId("navigation.synthetic"),
        {
            NavigationNode{
                NavigationNodeId("node.a"),
                {0.0F, 0.0F, 0.0F},
                {NavigationNodeId("node.b")}},
            NavigationNode{
                NavigationNodeId("node.b"),
                {1.0F, 0.0F, 0.0F},
                {NavigationNodeId("node.a")}}
        }};
}

}

int main() {
    using namespace contract::navigation;

    NavigationValidator validator;
    CONTRACT_EXPECT(validator.validate(valid_graph()).empty());

    auto duplicates = valid_graph();
    duplicates.nodes.push_back(duplicates.nodes.front());
    CONTRACT_EXPECT(contains_issue(
        validator.validate(duplicates),
        NavigationIssueCode::duplicate_node_identifier));

    auto missing_neighbor = valid_graph();
    missing_neighbor.nodes.front().neighbors = {
        NavigationNodeId("node.missing")};
    CONTRACT_EXPECT(contains_issue(
        validator.validate(missing_neighbor),
        NavigationIssueCode::missing_neighbor_reference));

    auto invalid_position = valid_graph();
    invalid_position.nodes.front().position[1] =
        std::numeric_limits<float>::quiet_NaN();
    CONTRACT_EXPECT(contains_issue(
        validator.validate(invalid_position),
        NavigationIssueCode::invalid_position));

    return contract::test::finish();
}

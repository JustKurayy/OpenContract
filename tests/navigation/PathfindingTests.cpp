#include "TestSupport.hpp"

#include <contract/navigation/Pathfinding.hpp>

#include <cmath>
#include <cstddef>
#include <string>

namespace {

contract::navigation::NavigationGraph graph_with_alternatives() {
    using namespace contract::navigation;
    return {
        NavigationGraphId("navigation.synthetic"),
        {
            {
                NavigationNodeId("node.start"),
                {0.0F, 0.0F, 0.0F},
                {NavigationNodeId("node.beta"), NavigationNodeId("node.alpha")}
            },
            {
                NavigationNodeId("node.alpha"),
                {1.0F, 1.0F, 0.0F},
                {NavigationNodeId("node.goal")}
            },
            {
                NavigationNodeId("node.beta"),
                {1.0F, -1.0F, 0.0F},
                {NavigationNodeId("node.goal")}
            },
            {
                NavigationNodeId("node.goal"),
                {2.0F, 0.0F, 0.0F},
                {}
            },
            {
                NavigationNodeId("node.isolated"),
                {10.0F, 0.0F, 0.0F},
                {}
            }
        }};
}

}

int main() {
    using namespace contract::navigation;

    const NavigationPathfinder pathfinder;
    const auto graph = graph_with_alternatives();
    const auto path = pathfinder.shortest_path(
        graph,
        NavigationNodeId("node.start"),
        NavigationNodeId("node.goal"));
    CONTRACT_EXPECT(path.has_value());
    CONTRACT_EXPECT_EQ(path.value().nodes.size(), std::size_t{3});
    CONTRACT_EXPECT_EQ(
        path.value().nodes[0].value(),
        std::string("node.start"));
    CONTRACT_EXPECT_EQ(
        path.value().nodes[1].value(),
        std::string("node.alpha"));
    CONTRACT_EXPECT_EQ(
        path.value().nodes[2].value(),
        std::string("node.goal"));
    CONTRACT_EXPECT(
        std::abs(path.value().total_cost - (2.0 * std::sqrt(2.0))) < 0.000001);

    const auto same_node = pathfinder.shortest_path(
        graph,
        NavigationNodeId("node.start"),
        NavigationNodeId("node.start"));
    CONTRACT_EXPECT(same_node.has_value());
    CONTRACT_EXPECT_EQ(same_node.value().nodes.size(), std::size_t{1});
    CONTRACT_EXPECT_EQ(same_node.value().total_cost, 0.0);

    const auto no_path = pathfinder.shortest_path(
        graph,
        NavigationNodeId("node.start"),
        NavigationNodeId("node.isolated"));
    CONTRACT_EXPECT(!no_path.has_value());
    CONTRACT_EXPECT_EQ(
        no_path.error().code,
        PathQueryErrorCode::no_path);

    const auto missing_start = pathfinder.shortest_path(
        graph,
        NavigationNodeId("node.missing"),
        NavigationNodeId("node.goal"));
    CONTRACT_EXPECT(!missing_start.has_value());
    CONTRACT_EXPECT_EQ(
        missing_start.error().code,
        PathQueryErrorCode::start_not_found);

    auto invalid_graph = graph;
    invalid_graph.nodes[0].neighbors.push_back(
        NavigationNodeId("node.missing"));
    const auto invalid = pathfinder.shortest_path(
        invalid_graph,
        NavigationNodeId("node.start"),
        NavigationNodeId("node.goal"));
    CONTRACT_EXPECT(!invalid.has_value());
    CONTRACT_EXPECT_EQ(
        invalid.error().code,
        PathQueryErrorCode::invalid_graph);

    return contract::test::finish();
}

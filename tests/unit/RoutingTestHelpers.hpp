#pragma once

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "TestHelpers.hpp"
#include "opticalnet/routing/RoutingEngine.hpp"

namespace opticalnet::testing {

// A link with explicit physical length and administrative cost, so the three metrics can disagree.
inline FiberLink routedLink(std::uint32_t id, std::uint32_t from, std::uint32_t to, double km, double admin,
                            LinkDirection direction = LinkDirection::Bidirectional, std::uint32_t channels = 40) {
    return must(FiberLink::create(LinkId{id}, "r" + std::to_string(id), NodeId{from}, NodeId{to}, km, 0.2, channels,
                                  direction, admin));
}

inline std::vector<NodeId> nodeIds(std::initializer_list<std::uint32_t> values) {
    std::vector<NodeId> out;
    for (auto v : values) out.push_back(NodeId{v});
    return out;
}

inline std::vector<LinkId> linkIds(std::initializer_list<std::uint32_t> values) {
    std::vector<LinkId> out;
    for (auto v : values) out.push_back(LinkId{v});
    return out;
}

// Independent re-check of a returned path against the topology: endpoints, continuity,
// direction, no repeated nodes, and cost recomputed link by link under the metric.
inline void expectValidPath(const Topology& t, const Path& p, NodeId source, NodeId destination, CostMetric metric) {
    EXPECT_EQ(p.source(), source);
    EXPECT_EQ(p.destination(), destination);
    EXPECT_EQ(p.metric(), metric);
    ASSERT_EQ(p.nodes().size(), p.links().size() + 1);
    EXPECT_EQ(p.hopCount(), p.links().size());
    double cost = 0.0;
    for (std::size_t i = 0; i < p.links().size(); ++i) {
        const FiberLink* link = t.findLink(p.links()[i]);
        ASSERT_NE(link, nullptr);
        EXPECT_TRUE(link->allowsTraversal(p.nodes()[i], p.nodes()[i + 1])) << "hop " << i << " violates link direction";
        cost += linkCost(*link, metric);
    }
    EXPECT_DOUBLE_EQ(cost, p.cost());
    std::set<NodeId> distinct(p.nodes().begin(), p.nodes().end());
    EXPECT_EQ(distinct.size(), p.nodes().size()) << "path revisits a node";
}

}  // namespace opticalnet::testing

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <set>

#include "TestHelpers.hpp"
#include "opticalnet/topology/LinkCost.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

std::vector<NodeId> ids(std::initializer_list<std::uint32_t> values) {
    std::vector<NodeId> out;
    for (auto v : values) out.push_back(NodeId{v});
    return out;
}

//      1 --l1-- 2 --l2-- 3        4 (isolated)
//      |                 |
//      +-------l3--------+
Topology triangleWithIsolatedNode() {
    return makeTopology({1, 2, 3, 4}, {makeLink(1, 1, 2), makeLink(2, 2, 3), makeLink(3, 3, 1)});
}

}  // namespace

// ----------------------------------------------------------------- adjacency

TEST(TopologyGraph, NeighborsAreDistinctAndSorted) {
    const Topology t = triangleWithIsolatedNode();
    EXPECT_EQ(t.neighbors(NodeId{1}), ids({2, 3}));
    EXPECT_EQ(t.neighbors(NodeId{2}), ids({1, 3}));
    EXPECT_TRUE(t.neighbors(NodeId{4}).empty());
    EXPECT_TRUE(t.neighbors(NodeId{99}).empty());
}

TEST(TopologyGraph, OutgoingExposesNeighborAndLinkSortedByLinkId) {
    const Topology t = triangleWithIsolatedNode();
    const auto edges = t.outgoing(NodeId{1});
    ASSERT_EQ(edges.size(), 2u);
    EXPECT_EQ(edges[0], (Adjacency{NodeId{2}, LinkId{1}}));
    EXPECT_EQ(edges[1], (Adjacency{NodeId{3}, LinkId{3}}));
}

TEST(TopologyGraph, AdjacencyOrderIsIndependentOfInsertionOrder) {
    Topology t = makeTopology({1, 2, 3}, {});
    ASSERT_OK(t.addLink(makeLink(9, 1, 3)));
    ASSERT_OK(t.addLink(makeLink(4, 1, 2)));
    const auto edges = t.outgoing(NodeId{1});
    ASSERT_EQ(edges.size(), 2u);
    EXPECT_EQ(edges[0].link, LinkId{4});
    EXPECT_EQ(edges[1].link, LinkId{9});
}

TEST(TopologyGraph, LinksBetweenAndDirectLink) {
    const Topology t = triangleWithIsolatedNode();
    EXPECT_EQ(t.linksBetween(NodeId{1}, NodeId{2}), (std::vector<LinkId>{LinkId{1}}));
    EXPECT_EQ(t.linksBetween(NodeId{2}, NodeId{1}), (std::vector<LinkId>{LinkId{1}}));
    EXPECT_TRUE(t.hasDirectLink(NodeId{1}, NodeId{3}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{1}, NodeId{4}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{1}, NodeId{99}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{99}, NodeId{1}));
    EXPECT_TRUE(t.linksBetween(NodeId{1}, NodeId{4}).empty());
}

TEST(TopologyGraph, IncidentListsEveryTouchingLink) {
    const Topology t = triangleWithIsolatedNode();
    EXPECT_EQ(t.degree(NodeId{1}), 2u);
    EXPECT_EQ(t.degree(NodeId{4}), 0u);
    EXPECT_EQ(t.degree(NodeId{99}), 0u);
    for (const Adjacency& a : t.incident(NodeId{1})) {
        const FiberLink* link = t.findLink(a.link);
        ASSERT_NE(link, nullptr);
        EXPECT_TRUE(link->touches(NodeId{1}));
        EXPECT_TRUE(link->touches(a.neighbor));
    }
}

// ----------------------------------------------------------------- direction

TEST(LinkDirection, BidirectionalIsTheDefault) {
    const FiberLink l = makeLink(1, 1, 2);
    EXPECT_EQ(l.direction(), LinkDirection::Bidirectional);
    EXPECT_TRUE(l.allowsTraversal(NodeId{1}, NodeId{2}));
    EXPECT_TRUE(l.allowsTraversal(NodeId{2}, NodeId{1}));
}

TEST(LinkDirection, DirectedAllowsSourceToTargetOnly) {
    const FiberLink l = makeDirectedLink(1, 1, 2);
    EXPECT_EQ(l.direction(), LinkDirection::Directed);
    EXPECT_TRUE(l.allowsTraversal(NodeId{1}, NodeId{2}));
    EXPECT_FALSE(l.allowsTraversal(NodeId{2}, NodeId{1}));
    EXPECT_FALSE(l.allowsTraversal(NodeId{1}, NodeId{3}));
}

TEST(LinkDirection, BidirectionalLinkAppearsInBothOutgoingLists) {
    const Topology t = makeTopology({1, 2}, {makeLink(1, 1, 2)});
    EXPECT_TRUE(t.hasDirectLink(NodeId{1}, NodeId{2}));
    EXPECT_TRUE(t.hasDirectLink(NodeId{2}, NodeId{1}));
    EXPECT_EQ(t.neighbors(NodeId{2}), ids({1}));
}

TEST(LinkDirection, DirectedLinkAppearsOnlyInSourceOutgoingList) {
    const Topology t = makeTopology({1, 2}, {makeDirectedLink(1, 1, 2)});
    EXPECT_TRUE(t.hasDirectLink(NodeId{1}, NodeId{2}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{2}, NodeId{1}));
    EXPECT_EQ(t.neighbors(NodeId{1}), ids({2}));
    EXPECT_TRUE(t.neighbors(NodeId{2}).empty());
    EXPECT_TRUE(t.outgoing(NodeId{2}).empty());
}

TEST(LinkDirection, DirectedLinkStillCountsInIncidentAndDegreeOfBothEnds) {
    const Topology t = makeTopology({1, 2}, {makeDirectedLink(1, 1, 2)});
    EXPECT_EQ(t.degree(NodeId{1}), 1u);
    EXPECT_EQ(t.degree(NodeId{2}), 1u);
    ASSERT_EQ(t.incident(NodeId{2}).size(), 1u);
    EXPECT_EQ(t.incident(NodeId{2})[0].neighbor, NodeId{1});
}

TEST(LinkDirection, MixedTopologyRespectsEachLinksDirection) {
    // 1 <-> 2 (bidirectional), 2 -> 3 (directed)
    const Topology t = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeDirectedLink(2, 2, 3)});
    EXPECT_TRUE(t.isReachable(NodeId{1}, NodeId{3}));
    EXPECT_FALSE(t.isReachable(NodeId{3}, NodeId{1}));
    EXPECT_TRUE(t.isReachable(NodeId{2}, NodeId{1}));
}

TEST(LinkDirection, OppositeDirectedLinksFormAnAsymmetricPair) {
    const Topology t = makeTopology({1, 2}, {makeDirectedLink(1, 1, 2), makeDirectedLink(2, 2, 1)});
    EXPECT_EQ(t.linksBetween(NodeId{1}, NodeId{2}), (std::vector<LinkId>{LinkId{1}}));
    EXPECT_EQ(t.linksBetween(NodeId{2}, NodeId{1}), (std::vector<LinkId>{LinkId{2}}));
}

TEST(LinkDirection, HasReadableNames) {
    EXPECT_EQ(toString(LinkDirection::Bidirectional), "bidirectional");
    EXPECT_EQ(toString(LinkDirection::Directed), "directed");
}

// ---------------------------------------------------------------------- cost

TEST(LinkCost, MetricsReadDifferentLinkProperties) {
    const auto link = must(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 250.0, 0.2, 10,
                                             LinkDirection::Bidirectional, 7.5));
    EXPECT_DOUBLE_EQ(linkCost(link, CostMetric::HopCount), 1.0);
    EXPECT_DOUBLE_EQ(linkCost(link, CostMetric::Distance), 250.0);
    EXPECT_DOUBLE_EQ(linkCost(link, CostMetric::Administrative), 7.5);
}

TEST(LinkCost, DefaultAdministrativeCostIsOne) {
    EXPECT_DOUBLE_EQ(makeLink(1, 1, 2).administrativeCost(), 1.0);
}

TEST(LinkCost, RejectsNonPositiveOrNonFiniteCost) {
    for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 10, 0.2, 10, LinkDirection::Bidirectional, bad),
                     ErrorCode::InvalidArgument)
            << "cost " << bad;
    }
}

TEST(LinkCost, MetricNamesAreReadable) {
    EXPECT_EQ(toString(CostMetric::HopCount), "hop_count");
    EXPECT_EQ(toString(CostMetric::Distance), "distance");
    EXPECT_EQ(toString(CostMetric::Administrative), "administrative");
}

// -------------------------------------------------------------- connectivity

TEST(TopologyConnectivity, ReachabilityFollowsLinks) {
    const Topology t = triangleWithIsolatedNode();
    EXPECT_TRUE(t.isReachable(NodeId{1}, NodeId{3}));
    EXPECT_TRUE(t.isReachable(NodeId{1}, NodeId{1})) << "a node reaches itself";
    EXPECT_FALSE(t.isReachable(NodeId{1}, NodeId{4}));
    EXPECT_FALSE(t.isReachable(NodeId{99}, NodeId{1}));
    EXPECT_FALSE(t.isReachable(NodeId{1}, NodeId{99}));
    EXPECT_FALSE(t.isReachable(NodeId{99}, NodeId{99}));
}

TEST(TopologyConnectivity, ReachabilityWorksOverMultipleHops) {
    const Topology t = makeTopology({1, 2, 3, 4, 5}, {makeLink(1, 1, 2), makeLink(2, 2, 3), makeLink(3, 3, 4)});
    EXPECT_TRUE(t.isReachable(NodeId{1}, NodeId{4}));
    EXPECT_TRUE(t.isReachable(NodeId{4}, NodeId{1}));
    EXPECT_FALSE(t.isReachable(NodeId{1}, NodeId{5}));
}

TEST(TopologyConnectivity, ComponentsAreSortedAndDeterministic) {
    const Topology t = makeTopology({5, 3, 1, 2, 4}, {makeLink(1, 5, 1), makeLink(2, 3, 2)});
    const auto components = t.connectedComponents();
    ASSERT_EQ(components.size(), 3u);
    EXPECT_EQ(components[0], ids({1, 5}));
    EXPECT_EQ(components[1], ids({2, 3}));
    EXPECT_EQ(components[2], ids({4}));
}

TEST(TopologyConnectivity, ConnectedVersusDisconnected) {
    EXPECT_TRUE(makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeLink(2, 2, 3)}).isConnected());
    EXPECT_FALSE(triangleWithIsolatedNode().isConnected());
    EXPECT_TRUE(makeTopology({1}, {}).isConnected()) << "single node";
    EXPECT_TRUE(Topology{}.isConnected()) << "empty topology is vacuously connected";
    EXPECT_FALSE(makeTopology({1, 2}, {}).isConnected());
}

TEST(TopologyConnectivity, RemovingALinkCanDisconnectTheTopology) {
    Topology t = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeLink(2, 2, 3)});
    ASSERT_TRUE(t.isConnected());
    ASSERT_OK(t.removeLink(LinkId{2}));
    EXPECT_FALSE(t.isConnected());
    EXPECT_FALSE(t.isReachable(NodeId{1}, NodeId{3}));
}

TEST(TopologyConnectivity, WeakVersusStrongConnectivity) {
    const Topology chain = makeTopology({1, 2, 3}, {makeDirectedLink(1, 1, 2), makeDirectedLink(2, 2, 3)});
    EXPECT_TRUE(chain.isConnected()) << "direction ignored for physical connectivity";
    EXPECT_FALSE(chain.isStronglyConnected());

    const Topology cycle =
        makeTopology({1, 2, 3}, {makeDirectedLink(1, 1, 2), makeDirectedLink(2, 2, 3), makeDirectedLink(3, 3, 1)});
    EXPECT_TRUE(cycle.isStronglyConnected());

    const Topology undirected = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeLink(2, 2, 3)});
    EXPECT_TRUE(undirected.isStronglyConnected());

    EXPECT_FALSE(triangleWithIsolatedNode().isStronglyConnected());
    EXPECT_TRUE(Topology{}.isStronglyConnected());
}

TEST(TopologyConnectivity, StrongConnectivityDetectsASinkReachableOnlyOneWay) {
    // 1 <-> 2 and 2 -> 3: everything reaches 3, but 3 reaches nobody.
    const Topology t = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeDirectedLink(2, 2, 3)});
    EXPECT_FALSE(t.isStronglyConnected());
}

// --------------------------------------------------------------- consistency

// Property test: after a long random sequence of add/remove operations the adjacency
// index must equal what a brute-force scan of the Network's links says.
TEST(TopologyConsistency, AdjacencyAlwaysMatchesTheUnderlyingNetwork) {
    std::mt19937 rng(12345);
    Topology t;
    constexpr std::uint32_t kNodes = 12;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));

    std::uint32_t nextLink = 1;
    for (int step = 0; step < 600; ++step) {
        const bool add = t.linkCount() < 5 || rng() % 3 != 0;
        if (add) {
            const auto a = static_cast<std::uint32_t>(rng() % kNodes) + 1;
            auto b = static_cast<std::uint32_t>(rng() % kNodes) + 1;
            if (a == b) b = a % kNodes + 1;
            const bool directed = rng() % 2 == 0;
            ASSERT_OK(t.addLink(directed ? makeDirectedLink(nextLink, a, b) : makeLink(nextLink, a, b)));
            ++nextLink;
        } else {
            const auto& links = t.network().links();
            auto it = links.begin();
            std::advance(it, static_cast<long>(rng() % links.size()));
            const LinkId victim = it->first;
            ASSERT_OK(t.removeLink(victim));
        }
    }

    for (std::uint32_t n = 1; n <= kNodes; ++n) {
        std::set<std::pair<std::uint32_t, std::uint32_t>> expectedOut;  // (link, neighbor)
        std::set<std::pair<std::uint32_t, std::uint32_t>> expectedInc;
        for (const auto& [lid, link] : t.network().links()) {
            if (link.allowsTraversal(NodeId{n}, link.source() == NodeId{n} ? link.target() : link.source()) &&
                link.touches(NodeId{n})) {
                expectedOut.insert({lid.value(), (link.source() == NodeId{n} ? link.target() : link.source()).value()});
            }
            if (link.touches(NodeId{n})) {
                expectedInc.insert({lid.value(), (link.source() == NodeId{n} ? link.target() : link.source()).value()});
            }
        }
        std::set<std::pair<std::uint32_t, std::uint32_t>> actualOut;
        std::set<std::pair<std::uint32_t, std::uint32_t>> actualInc;
        for (const Adjacency& a : t.outgoing(NodeId{n})) actualOut.insert({a.link.value(), a.neighbor.value()});
        for (const Adjacency& a : t.incident(NodeId{n})) actualInc.insert({a.link.value(), a.neighbor.value()});
        EXPECT_EQ(actualOut, expectedOut) << "outgoing mismatch at node " << n;
        EXPECT_EQ(actualInc, expectedInc) << "incident mismatch at node " << n;

        const auto out = t.outgoing(NodeId{n});
        EXPECT_TRUE(std::ranges::is_sorted(out, [](const Adjacency& x, const Adjacency& y) { return x.link < y.link; }));
    }
}

TEST(TopologyConsistency, LargeRingIsBuiltAndQueriedQuickly) {
    constexpr std::uint32_t kNodes = 5000;
    Topology t;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addLink(makeLink(i, i, i % kNodes + 1)));
    EXPECT_TRUE(t.isConnected());
    EXPECT_TRUE(t.isStronglyConnected());
    EXPECT_EQ(t.neighbors(NodeId{1}), ids({2, kNodes}));
    EXPECT_TRUE(t.validate().clean());
}

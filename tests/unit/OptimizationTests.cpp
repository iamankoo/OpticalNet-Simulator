// Phase 10: regression tests for the measured optimizations (cached link pointers in the adjacency index, the flat
// Dijkstra label table, the hash-based allocation map). They check that behaviour is unchanged where the new
// structures could go wrong: copies, moves, assignment, mutation after copy, large searches, ordering.
#include <gtest/gtest.h>

#include <memory>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/resources/ResourceManager.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// w x h grid, node id = y*w + x + 1, unit links (1 km, cost 1).
Topology grid(int w, int h) {
    Topology t;
    for (int i = 1; i <= w * h; ++i) EXPECT_TRUE(t.addNode(makeNode(static_cast<std::uint32_t>(i))).ok());
    std::uint32_t link = 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto id = static_cast<std::uint32_t>(y * w + x + 1);
            if (x + 1 < w) {
                EXPECT_TRUE(t.addLink(routedLink(link++, id, id + 1, 1.0, 1.0)).ok());
            }
            if (y + 1 < h) {
                EXPECT_TRUE(t.addLink(routedLink(link++, id, id + static_cast<std::uint32_t>(w), 1.0, 1.0)).ok());
            }
        }
    return t;
}

Topology triangle() {
    return makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 10, 1), routedLink(2, 2, 3, 10, 1), routedLink(3, 1, 3, 50, 9)});
}

bool pointsIntoOwnNetwork(const Topology& t) {
    for (const auto& [id, node] : t.network().nodes()) {
        for (const Adjacency& edge : t.outgoing(id)) {
            if (edge.fiber == nullptr || edge.fiber != t.findLink(edge.link)) return false;
        }
        for (const Adjacency& edge : t.incident(id)) {
            if (edge.fiber == nullptr || edge.fiber != t.findLink(edge.link)) return false;
        }
    }
    return true;
}

}  // namespace

TEST(Optimization, AdjacencyEntriesPointAtTheirOwnTopologysLinks) {
    const Topology t = triangle();
    EXPECT_TRUE(pointsIntoOwnNetwork(t));
}

TEST(Optimization, AdjacencyEqualityIgnoresTheCachedPointer) {
    const Topology t = triangle();
    const Adjacency cached = t.outgoing(NodeId{1})[0];
    EXPECT_NE(cached.fiber, nullptr);
    EXPECT_EQ(cached, (Adjacency{cached.neighbor, cached.link}));  // hand-built entry without a pointer
    EXPECT_FALSE(cached == (Adjacency{NodeId{3}, cached.link}));
}

TEST(Optimization, CopiedTopologyOwnsItsAdjacencyPointers) {
    auto original = std::make_unique<Topology>(triangle());
    Topology copy(*original);
    EXPECT_TRUE(pointsIntoOwnNetwork(copy));
    EXPECT_NE(copy.outgoing(NodeId{1})[0].fiber, original->outgoing(NodeId{1})[0].fiber);
    original.reset();  // destroying the original must not affect the copy
    EXPECT_TRUE(pointsIntoOwnNetwork(copy));
    auto path = RoutingEngine{}.findPath(copy, NodeId{1}, NodeId{3});
    ASSERT_TRUE(path.ok());
    EXPECT_EQ(path.value().links(), linkIds({1, 2}));
}

TEST(Optimization, MutatingACopyDoesNotChangeTheOriginal) {
    const Topology original = triangle();
    Topology copy = original;
    ASSERT_TRUE(copy.removeLink(LinkId{1}).ok());
    ASSERT_TRUE(copy.addLink(routedLink(9, 2, 3, 1.0, 1.0)).ok());
    EXPECT_TRUE(pointsIntoOwnNetwork(copy));
    EXPECT_TRUE(pointsIntoOwnNetwork(original));
    EXPECT_EQ(original.linkCount(), 3u);
    EXPECT_EQ(RoutingEngine{}.findPath(original, NodeId{1}, NodeId{3}).value().links(), linkIds({1, 2}));
    EXPECT_EQ(RoutingEngine{}.findPath(copy, NodeId{1}, NodeId{3}).value().links(), linkIds({3}));
}

TEST(Optimization, CopyAssignmentRebuildsTheIndexAndSelfAssignmentIsHarmless) {
    Topology a = triangle();
    Topology b = grid(3, 3);
    b = a;
    EXPECT_TRUE(pointsIntoOwnNetwork(b));
    EXPECT_EQ(b.nodeCount(), 3u);
    Topology& alias = b;
    b = alias;
    EXPECT_TRUE(pointsIntoOwnNetwork(b));
    EXPECT_EQ(RoutingEngine{}.findPath(b, NodeId{1}, NodeId{3}).value().links(), linkIds({1, 2}));
}

TEST(Optimization, MovedTopologyKeepsValidAdjacency) {
    Topology source = grid(6, 6);
    Topology moved(std::move(source));
    EXPECT_TRUE(pointsIntoOwnNetwork(moved));
    Topology assigned;
    assigned = std::move(moved);
    EXPECT_TRUE(pointsIntoOwnNetwork(assigned));
    auto path = RoutingEngine{}.findPath(assigned, NodeId{1}, NodeId{36});
    ASSERT_TRUE(path.ok());
    EXPECT_DOUBLE_EQ(path.value().cost(), 10.0);
}

TEST(Optimization, NetworkConstructorIndexesTheStoredLinks) {
    Network net;
    for (std::uint32_t i = 1; i <= 3; ++i) ASSERT_TRUE(net.addNode(makeNode(i)).ok());
    ASSERT_TRUE(net.addLink(routedLink(1, 1, 2, 5, 1)).ok());
    ASSERT_TRUE(net.addLink(routedLink(2, 2, 3, 5, 1)).ok());
    const Topology t(std::move(net));
    EXPECT_TRUE(pointsIntoOwnNetwork(t));
    EXPECT_EQ(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{3}).value().links(), linkIds({1, 2}));
}

TEST(Optimization, LargeSearchesGrowTheLabelTableCorrectly) {
    // 80x80 = 6400 nodes: the search table grows many times. Manhattan distance is the exact optimum.
    const Topology t = grid(80, 80);
    for (auto metric : {CostMetric::HopCount, CostMetric::Distance, CostMetric::Administrative}) {
        RoutingConstraints c;
        c.metric = metric;
        auto path = RoutingEngine{}.findPath(t, NodeId{1}, NodeId{6400}, c);
        ASSERT_TRUE(path.ok());
        EXPECT_DOUBLE_EQ(path.value().cost(), 158.0);
        expectValidPath(t, path.value(), NodeId{1}, NodeId{6400}, metric);
    }
    // The layered (hop-limited) search uses (node, layer) keys, which multiplies the number of labels.
    RoutingConstraints limited;
    limited.metric = CostMetric::Distance;
    limited.maxHops = 158;
    auto exact = RoutingEngine{}.findPath(t, NodeId{1}, NodeId{6400}, limited);
    ASSERT_TRUE(exact.ok());
    EXPECT_EQ(exact.value().hopCount(), 158u);
    limited.maxHops = 157;
    EXPECT_EQ(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{6400}, limited).error().code, ErrorCode::NoFeasibleRoute);
}

TEST(Optimization, RoutingResultsAreIdenticalOnACopy) {
    const Topology original = grid(25, 25);
    const Topology copy = original;
    RoutingEngine engine;
    for (std::uint32_t s : {1u, 77u, 300u, 625u}) {
        for (std::uint32_t d : {2u, 100u, 624u, 1u}) {
            if (s == d) continue;
            auto a = engine.findPath(original, NodeId{s}, NodeId{d});
            auto b = engine.findPath(copy, NodeId{s}, NodeId{d});
            ASSERT_TRUE(a.ok() && b.ok());
            EXPECT_EQ(a.value(), b.value());
        }
    }
}

TEST(Optimization, AllocationsAreReportedInAscendingLinkOrder) {
    const Topology t = grid(6, 6);
    ResourceManager rm(t);
    for (std::uint32_t link : {40u, 7u, 25u, 1u, 33u, 12u}) {
        const LinkId id{link};
        ASSERT_TRUE(rm.allocate(std::span<const LinkId>(&id, 1), 1).ok());
    }
    const auto allocations = rm.allocations();
    ASSERT_EQ(allocations.size(), 6u);
    for (std::size_t i = 1; i < allocations.size(); ++i) EXPECT_LT(allocations[i - 1].first, allocations[i].first);
    EXPECT_EQ(rm.totalAllocated(), 6u);
    EXPECT_TRUE(rm.validate().clean());
    const LinkId one{1};
    ASSERT_TRUE(rm.release(std::span<const LinkId>(&one, 1), 1).ok());
    EXPECT_EQ(rm.allocations().front().first, LinkId{7});  // a link with nothing allocated disappears
}

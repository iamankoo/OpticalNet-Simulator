#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <random>

#include "RoutingTestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

struct Best {
    double cost = std::numeric_limits<double>::infinity();
    std::size_t hops = 0;
    bool found = false;
};

// Exhaustive reference: enumerate every simple path (over individual links, so parallel links count)
// and keep the best (cost, hops). Simple paths suffice because all costs are positive.
void enumerate(const Topology& t, NodeId at, NodeId target, const RoutingConstraints& c, std::set<NodeId>& visited,
               double cost, std::size_t hops, Best& best) {
    if (c.maxCost && cost > *c.maxCost) return;
    if (c.maxHops && hops > *c.maxHops) return;
    if (at == target) {
        if (!best.found || cost < best.cost || (cost == best.cost && hops < best.hops)) best = {cost, hops, true};
        return;
    }
    for (const Adjacency& edge : t.outgoing(at)) {
        const FiberLink& link = *t.findLink(edge.link);
        if (visited.contains(edge.neighbor) || c.blockedNodes.contains(edge.neighbor) || c.blockedLinks.contains(edge.link) ||
            link.capacityChannels() < c.minLinkCapacityChannels)
            continue;
        visited.insert(edge.neighbor);
        enumerate(t, edge.neighbor, target, c, visited, cost + linkCost(link, c.metric), hops + 1, best);
        visited.erase(edge.neighbor);
    }
}

Best bruteForce(const Topology& t, NodeId s, NodeId d, const RoutingConstraints& c) {
    Best best;
    if (c.blockedNodes.contains(s) || c.blockedNodes.contains(d)) return best;
    std::set<NodeId> visited{s};
    enumerate(t, s, d, c, visited, 0.0, 0, best);
    return best;
}

}  // namespace

// Compares the router against exhaustive search on many random multigraphs containing
// directed, bidirectional and parallel links, under random constraint combinations.
TEST(RoutingProperty, MatchesExhaustiveSearchOnRandomGraphs) {
    std::mt19937 rng(2024);
    const CostMetric metrics[] = {CostMetric::HopCount, CostMetric::Distance, CostMetric::Administrative};
    int feasible = 0, noRoute = 0, noFeasible = 0, hopLimitBound = 0;

    for (int trial = 0; trial < 5000; ++trial) {
        constexpr std::uint32_t kNodes = 9;
        Topology t;
        for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
        const int linkCount = 8 + static_cast<int>(rng() % 10);
        // In "convex" trials a link's cost grows with the square of the id distance it spans, so a
        // chain of short links is cheaper than a jump: the cheapest path has many hops and a hop
        // limit often forces a costlier, shorter route.
        const bool convex = rng() % 2 == 0;
        for (int l = 1; l <= linkCount; ++l) {
            const auto a = static_cast<std::uint32_t>(rng() % kNodes) + 1;
            auto b = static_cast<std::uint32_t>(rng() % kNodes) + 1;
            if (a == b) b = a % kNodes + 1;
            const auto dir = rng() % 3 == 0 ? LinkDirection::Directed : LinkDirection::Bidirectional;
            const double span = a > b ? a - b : b - a;
            const double km = convex ? span * span : static_cast<double>(1 + rng() % 20);
            const double admin = convex ? span * span : static_cast<double>(1 + rng() % 20);
            ASSERT_OK(t.addLink(routedLink(static_cast<std::uint32_t>(l), a, b, km, admin, dir, 10 * (1 + rng() % 4))));
        }

        RoutingConstraints c;
        c.metric = metrics[rng() % 3];
        if (rng() % 10 < 7) c.maxHops = static_cast<std::uint32_t>(1 + rng() % 3);
        if (rng() % 3 == 0) c.maxCost = static_cast<double>(rng() % 20);
        if (rng() % 4 == 0) c.minLinkCapacityChannels = 10 * (1 + static_cast<std::uint32_t>(rng() % 4));
        if (rng() % 4 == 0) c.blockedNodes.insert(NodeId{static_cast<std::uint32_t>(rng() % kNodes) + 1});
        if (rng() % 4 == 0) c.blockedLinks.insert(LinkId{static_cast<std::uint32_t>(rng() % linkCount) + 1});

        const NodeId s{static_cast<std::uint32_t>(rng() % kNodes) + 1};
        const NodeId d{static_cast<std::uint32_t>(rng() % kNodes) + 1};

        const Best expected = bruteForce(t, s, d, c);
        const auto actual = RoutingEngine{}.findPath(t, s, d, c);
        SCOPED_TRACE("trial " + std::to_string(trial));

        if (expected.found) {
            ASSERT_TRUE(actual.ok()) << actual.error().describe();
            EXPECT_DOUBLE_EQ(actual.value().cost(), expected.cost);
            EXPECT_EQ(actual.value().hopCount(), expected.hops) << "ties resolve to the fewest hops";
            expectValidPath(t, actual.value(), s, d, c.metric);
            if (c.maxHops) EXPECT_LE(actual.value().hopCount(), *c.maxHops);
            if (c.maxCost) EXPECT_LE(actual.value().cost(), *c.maxCost);
            for (LinkId l : actual.value().links()) {
                EXPECT_FALSE(c.blockedLinks.contains(l));
                EXPECT_GE(t.findLink(l)->capacityChannels(), c.minLinkCapacityChannels);
            }
            for (NodeId n : actual.value().nodes()) EXPECT_FALSE(c.blockedNodes.contains(n));
            ++feasible;
            if (c.maxHops) {
                RoutingConstraints loose = c;
                loose.maxHops.reset();
                const auto unlimited = RoutingEngine{}.findPath(t, s, d, loose);
                if (unlimited.ok() && unlimited.value().hopCount() > *c.maxHops) ++hopLimitBound;
            }
        } else {
            ASSERT_FALSE(actual.ok()) << "router found a path the exhaustive search says does not exist";
            RoutingConstraints none;
            none.metric = c.metric;
            const bool reachable = bruteForce(t, s, d, none).found;
            EXPECT_EQ(actual.error().code, reachable ? ErrorCode::NoFeasibleRoute : ErrorCode::NoRoute);
            (reachable ? noFeasible : noRoute)++;
        }
    }
    // The generator must actually exercise every outcome, otherwise the test proves little.
    EXPECT_GT(feasible, 100);
    EXPECT_GT(noRoute, 5);
    EXPECT_GT(noFeasible, 5);
    EXPECT_GT(hopLimitBound, 40) << "hop-limited search was needed in some trials";
}

// ------------------------------------------------------------ larger graphs

TEST(RoutingLarge, LinearChainOf5000Nodes) {
    constexpr std::uint32_t kNodes = 5000;
    Topology t;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    for (std::uint32_t i = 1; i < kNodes; ++i) ASSERT_OK(t.addLink(routedLink(i, i, i + 1, 2, 3)));

    const auto start = std::chrono::steady_clock::now();
    const Path forward = must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{kNodes}));
    const Path backward = must(RoutingEngine{}.findPath(t, NodeId{kNodes}, NodeId{1}));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(forward.hopCount(), kNodes - 1);
    EXPECT_DOUBLE_EQ(forward.cost(), 3.0 * (kNodes - 1));
    EXPECT_EQ(backward.hopCount(), kNodes - 1);
    EXPECT_EQ(backward.source(), NodeId{kNodes});
    expectValidPath(t, forward, NodeId{1}, NodeId{kNodes}, CostMetric::Administrative);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 10) << "pathological slowdown";
}

TEST(RoutingLarge, RingOf5000NodesPicksTheShorterSide) {
    constexpr std::uint32_t kNodes = 5000;
    Topology t;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addLink(routedLink(i, i, i % kNodes + 1, 1, 1)));

    const Path p = must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{1000}));
    EXPECT_EQ(p.hopCount(), 999u) << "clockwise: 999 hops; counter-clockwise: 4001";
    EXPECT_EQ(p.nodes().front(), NodeId{1});
    EXPECT_EQ(p.nodes()[1], NodeId{2});

    const Path far = must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{4000}));
    EXPECT_EQ(far.hopCount(), 1001u) << "counter-clockwise: 1001 hops; clockwise: 3999";
    EXPECT_EQ(far.nodes()[1], NodeId{kNodes});
    expectValidPath(t, far, NodeId{1}, NodeId{4000}, CostMetric::Administrative);
}

TEST(RoutingLarge, EqualCostRingTieIsStableAcrossRuns) {
    constexpr std::uint32_t kNodes = 5000;
    Topology t;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addLink(routedLink(i, i, i % kNodes + 1, 1, 1)));
    // Node 2501 is exactly opposite node 1: both directions cost 2500.
    const Path first = must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{2501}));
    EXPECT_EQ(first.hopCount(), 2500u);
    EXPECT_EQ(first.nodes()[1], NodeId{2}) << "predecessor 2500 < 2502 decides the tie";
    for (int i = 0; i < 3; ++i) EXPECT_EQ(must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{2501})), first);
}

TEST(RoutingLarge, GridOf4900NodesFindsAManhattanShortestPath) {
    constexpr std::uint32_t kSide = 70;
    const auto id = [](std::uint32_t r, std::uint32_t c) { return r * kSide + c + 1; };
    Topology t;
    for (std::uint32_t i = 1; i <= kSide * kSide; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    std::uint32_t link = 1;
    for (std::uint32_t r = 0; r < kSide; ++r) {
        for (std::uint32_t c = 0; c < kSide; ++c) {
            if (c + 1 < kSide) ASSERT_OK(t.addLink(routedLink(link++, id(r, c), id(r, c + 1), 5, 1)));
            if (r + 1 < kSide) ASSERT_OK(t.addLink(routedLink(link++, id(r, c), id(r + 1, c), 5, 1)));
        }
    }
    RoutingConstraints c;
    c.metric = CostMetric::Distance;
    const Path p = must(RoutingEngine{}.findPath(t, NodeId{id(0, 0)}, NodeId{id(kSide - 1, kSide - 1)}, c));
    EXPECT_EQ(p.hopCount(), 2u * (kSide - 1));
    EXPECT_DOUBLE_EQ(p.cost(), 5.0 * 2.0 * (kSide - 1));
    expectValidPath(t, p, NodeId{id(0, 0)}, NodeId{id(kSide - 1, kSide - 1)}, CostMetric::Distance);

    // Hop limit that binds on the grid's tie-heavy search: exact minimum is feasible, one less is not.
    c.maxHops = 2u * (kSide - 1);
    EXPECT_TRUE(RoutingEngine{}.findPath(t, NodeId{id(0, 0)}, NodeId{id(kSide - 1, kSide - 1)}, c).ok());
    c.maxHops = 2u * (kSide - 1) - 1;
    ASSERT_ERROR(RoutingEngine{}.findPath(t, NodeId{id(0, 0)}, NodeId{id(kSide - 1, kSide - 1)}, c),
                 ErrorCode::NoFeasibleRoute);
}

TEST(RoutingLarge, HopLimitedSearchOnALongChainWithAnExpressLink) {
    constexpr std::uint32_t kNodes = 400;
    Topology t;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    for (std::uint32_t i = 1; i < kNodes; ++i) ASSERT_OK(t.addLink(routedLink(i, i, i + 1, 1, 1)));
    ASSERT_OK(t.addLink(routedLink(kNodes, 1, kNodes, 1, 1000)));
    RoutingConstraints c;
    c.maxHops = 5;
    const Path p = must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{kNodes}, c));
    EXPECT_EQ(p.links(), linkIds({kNodes}));
    EXPECT_DOUBLE_EQ(p.cost(), 1000.0);
}

#include "ValidationTestUtils.hpp"
#include "opticalnet/routing/DijkstraRouter.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

struct Tally {
    int found = 0, noRoute = 0, noFeasible = 0, withFailures = 0, hopLimited = 0, costLimited = 0;
};

// Fails a few random links (and sometimes a node) so routing runs over a damaged network.
void damage(const Topology& t, FailureState& failures, Rng& rng) {
    std::vector<LinkId> links;
    for (const auto& e : t.network().links()) links.push_back(e.first);
    const std::uint32_t count = rng.uniformInt(1, 2);
    for (std::uint32_t i = 0; i < count && !links.empty(); ++i) (void)failures.failLink(links[rng.uniformIndex(links.size())]);
    if (rng.uniformInt(0, 3) == 0) (void)failures.failNode(NodeId{static_cast<std::uint32_t>(rng.uniformIndex(t.nodeCount())) + 1});
}

// Production Dijkstra versus exhaustive enumeration on one random topology.
void compareOnTopology(std::uint64_t seed, Tally& tally) {
    RandomTopologyConfig cfg;
    cfg.minNodes = 3;
    cfg.maxNodes = 7;
    cfg.extraLinksPerNode = 1.0;
    const Topology t = generateRandomTopology(cfg, seed);
    Rng rng(seed * 104729 + 17);
    FailureState failures(t);
    const bool overlay = seed % 2 == 0;
    if (overlay) {
        damage(t, failures, rng);
        ++tally.withFailures;
    }
    std::vector<NodeId> nodes;
    for (const auto& e : t.network().nodes()) nodes.push_back(e.first);

    for (int q = 0; q < 20; ++q) {
        const std::size_t si = rng.uniformIndex(nodes.size());
        std::size_t di = rng.uniformIndex(nodes.size() - 1);
        if (di >= si) ++di;
        const NodeId s = nodes[si], d = nodes[di];
        RoutingConstraints constraints = generateRandomConstraints(t, rng, 0.6);
        if (constraints.maxHops) ++tally.hopLimited;
        if (constraints.maxCost) ++tally.costLimited;

        RoutingConstraints production = constraints;
        if (overlay) production.linkFilter = [&failures](const FiberLink& l) { return failures.isLinkUsable(l); };

        const ReferenceRoute expected = exhaustiveRoute(t, s, d, constraints, overlay ? &failures : nullptr);  // the oracle gets the overlay directly
        const auto actual = DijkstraRouter{}.findPath(t, s, d, production);

        std::string context = "seed " + std::to_string(seed) + ", query " + std::to_string(q) + ": " + std::to_string(s.value()) + "->" +
                              std::to_string(d.value()) + ", metric " + std::string(toString(constraints.metric)) +
                              (constraints.maxHops ? ", maxHops " + std::to_string(*constraints.maxHops) : "") +
                              (constraints.maxCost ? ", maxCost " + std::to_string(*constraints.maxCost) : "") +
                              (overlay ? ", failures on" : "") + " (" + std::to_string(t.nodeCount()) + " nodes, " + std::to_string(t.linkCount()) + " links)";
        SCOPED_TRACE(context);

        if (expected.found) {
            ASSERT_TRUE(actual.ok()) << "oracle found cost " << expected.cost << " but routing failed: " << actual.error().describe();
            EXPECT_NEAR(actual.value().cost(), expected.cost, 1e-9) << "not the minimum-cost path";
            EXPECT_EQ(actual.value().hopCount(), expected.hops) << "ties must resolve to the fewest hops";
            const PathExpectation e{s, d, constraints.metric, &constraints, overlay ? &failures : nullptr};
            EXPECT_VALID(checkPath(t, actual.value(), e));
            ++tally.found;
        } else {
            ASSERT_FALSE(actual.ok()) << "routing returned a path although the oracle found none";
            const ErrorCode want = reachableByAnyPath(t, s, d) ? ErrorCode::NoFeasibleRoute : ErrorCode::NoRoute;
            EXPECT_EQ(actual.error().code, want) << actual.error().describe();
            ++(want == ErrorCode::NoRoute ? tally.noRoute : tally.noFeasible);
        }
    }
}

}  // namespace

TEST(ValRouting, DijkstraEqualsExhaustiveEnumerationOnRandomTopologies) {
    Tally tally;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        compareOnTopology(seed, tally);
        if (::testing::Test::HasFatalFailure()) return;
    }
    // The generator must exercise every outcome and constraint kind, otherwise the comparison proves little.
    EXPECT_GT(tally.found, 600);
    EXPECT_GT(tally.noFeasible, 100);
    EXPECT_GT(tally.noRoute, 5) << "directed links make some pairs unreachable";
    EXPECT_EQ(tally.withFailures, 50);
    EXPECT_GT(tally.hopLimited, 400);
    EXPECT_GT(tally.costLimited, 400);
}

TEST(ValRouting, CostLimitIsExactAtTheBoundary) {
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        Rng rng(seed);
        std::vector<NodeId> nodes;
        for (const auto& e : t.network().nodes()) nodes.push_back(e.first);
        for (int q = 0; q < 6; ++q) {
            const NodeId s = nodes[rng.uniformIndex(nodes.size())];
            NodeId d = nodes[rng.uniformIndex(nodes.size())];
            if (d == s) continue;
            RoutingConstraints c;
            c.metric = CostMetric::Distance;
            const ReferenceRoute best = exhaustiveRoute(t, s, d, c);
            if (!best.found || best.cost < 1.0) continue;
            c.maxCost = best.cost;  // exactly at the optimum: feasible
            ASSERT_TRUE(DijkstraRouter{}.findPath(t, s, d, c).ok()) << s.value() << "->" << d.value();
            c.maxCost = best.cost - 0.5;  // just below: infeasible, but a route exists
            const auto below = DijkstraRouter{}.findPath(t, s, d, c);
            ASSERT_FALSE(below.ok());
            EXPECT_EQ(below.error().code, ErrorCode::NoFeasibleRoute);
            ++checked;
        }
    }
    EXPECT_GT(checked, 100);
}

TEST(ValRouting, HopLimitIsExactAtTheBoundary) {
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        Rng rng(seed + 5);
        std::vector<NodeId> nodes;
        for (const auto& e : t.network().nodes()) nodes.push_back(e.first);
        for (int q = 0; q < 6; ++q) {
            const NodeId s = nodes[rng.uniformIndex(nodes.size())];
            const NodeId d = nodes[rng.uniformIndex(nodes.size())];
            if (d == s) continue;
            RoutingConstraints c;
            c.metric = CostMetric::HopCount;
            const ReferenceRoute fewest = exhaustiveRoute(t, s, d, c);
            if (!fewest.found || fewest.hops < 2) continue;
            c.maxHops = static_cast<std::uint32_t>(fewest.hops);
            ASSERT_TRUE(DijkstraRouter{}.findPath(t, s, d, c).ok());
            c.maxHops = static_cast<std::uint32_t>(fewest.hops) - 1;
            const auto below = DijkstraRouter{}.findPath(t, s, d, c);
            ASSERT_FALSE(below.ok());
            EXPECT_EQ(below.error().code, ErrorCode::NoFeasibleRoute);
            ++checked;
        }
    }
    EXPECT_GT(checked, 60);
}

TEST(ValRouting, TheChosenPathIsIndependentOfLinkInsertionOrder) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig bare;
        bare.attachEquipment = false;  // nodes are copied below; equipment attachments would not come along
        const Topology original = generateRandomTopology(bare, seed);
        // Rebuild the same network, adding the links in the opposite order.
        Topology reversed;
        for (const auto& e : original.network().nodes()) ASSERT_OK(reversed.addNode(e.second));
        std::vector<FiberLink> links;
        for (const auto& e : original.network().links()) links.push_back(e.second);
        for (auto it = links.rbegin(); it != links.rend(); ++it) ASSERT_OK(reversed.addLink(*it));
        ASSERT_VALID(checkTopology(reversed, false));

        Rng rng(seed);
        std::vector<NodeId> nodes;
        for (const auto& e : original.network().nodes()) nodes.push_back(e.first);
        for (int q = 0; q < 10; ++q) {
            const NodeId s = nodes[rng.uniformIndex(nodes.size())];
            const NodeId d = nodes[rng.uniformIndex(nodes.size())];
            if (s == d) continue;
            RoutingConstraints c;
            c.metric = CostMetric::Administrative;
            const auto a = DijkstraRouter{}.findPath(original, s, d, c);
            const auto b = DijkstraRouter{}.findPath(reversed, s, d, c);
            ASSERT_EQ(a.ok(), b.ok());
            if (a.ok()) {
                EXPECT_EQ(a.value(), b.value()) << s.value() << "->" << d.value();
            }
        }
    }
}

TEST(ValRouting, RoutingIsRepeatable) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 77);
    Rng rng(1);
    for (int q = 0; q < 30; ++q) {
        const NodeId s{static_cast<std::uint32_t>(rng.uniformIndex(t.nodeCount())) + 1};
        const NodeId d{static_cast<std::uint32_t>(rng.uniformIndex(t.nodeCount())) + 1};
        RoutingConstraints c = generateRandomConstraints(t, rng, 0.5);
        const auto first = DijkstraRouter{}.findPath(t, s, d, c);
        const auto second = DijkstraRouter{}.findPath(t, s, d, c);
        ASSERT_EQ(first.ok(), second.ok());
        if (first.ok()) EXPECT_EQ(first.value(), second.value());
        else EXPECT_EQ(first.error().code, second.error().code);
    }
}

TEST(ValRouting, PathsOnlyUseEdgesThatExistInTheTopology) {
    // Topology -> Routing boundary: every link of every returned path exists, joins the nodes it is used between, and is
    // traversed in a permitted direction (checkPath), for all metrics, on bigger random graphs (no oracle needed).
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig cfg;
        cfg.minNodes = 20;
        cfg.maxNodes = 50;
        const Topology t = generateRandomTopology(cfg, 500 + seed);
        Rng rng(seed);
        for (CostMetric m : {CostMetric::HopCount, CostMetric::Distance, CostMetric::Administrative}) {
            for (int q = 0; q < 8; ++q) {
                const NodeId s{static_cast<std::uint32_t>(rng.uniformIndex(t.nodeCount())) + 1};
                const NodeId d{static_cast<std::uint32_t>(rng.uniformIndex(t.nodeCount())) + 1};
                RoutingConstraints c;
                c.metric = m;
                const auto path = DijkstraRouter{}.findPath(t, s, d, c);
                if (!path.ok()) {
                    EXPECT_EQ(path.error().code, ErrorCode::NoRoute) << "unconstrained requests can only fail by unreachability";
                    continue;
                }
                EXPECT_VALID(checkPath(t, path.value(), PathExpectation{s, d, m, &c, nullptr}));
            }
        }
    }
}

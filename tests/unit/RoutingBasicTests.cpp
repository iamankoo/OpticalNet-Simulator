#include <gtest/gtest.h>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/routing/DijkstraRouter.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

constexpr NodeId A{1}, B{2}, C{3}, D{4};

RoutingConstraints withMetric(CostMetric metric) {
    RoutingConstraints c;
    c.metric = metric;
    return c;
}

// A --2-- B --2-- D
// |5
// C --1-- D
Topology specGraph() {
    return makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 20, 2), routedLink(2, 2, 4, 20, 2), routedLink(3, 1, 3, 50, 5),
                                       routedLink(4, 3, 4, 10, 1)});
}

// Three ways from 1 to 4, each best under a different metric:
//   direct      1-4        : 1 hop,  100 km, admin 10
//   via 2       1-2-4      : 2 hops,  60 km, admin 40
//   via 3       1-3-4      : 2 hops, 160 km, admin  2
Topology metricGraph() {
    return makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 4, 100, 10), routedLink(2, 1, 2, 30, 20),
                                       routedLink(3, 2, 4, 30, 20), routedLink(4, 1, 3, 80, 1), routedLink(5, 3, 4, 80, 1)});
}

}  // namespace

// --------------------------------------------------------------------- basics

TEST(Routing, OneHopRoute) {
    const Topology t = makeTopology({1, 2}, {routedLink(7, 1, 2, 40, 3)});
    const Path p = must(RoutingEngine{}.findPath(t, A, B));
    EXPECT_EQ(p.nodes(), nodeIds({1, 2}));
    EXPECT_EQ(p.links(), linkIds({7}));
    EXPECT_DOUBLE_EQ(p.cost(), 3.0);
    EXPECT_EQ(p.hopCount(), 1u);
    expectValidPath(t, p, A, B, CostMetric::Administrative);
}

TEST(Routing, ShortestOfTwoRoutesIsChosen) {
    const Topology t = specGraph();
    const Path p = must(RoutingEngine{}.findPath(t, A, D));
    EXPECT_EQ(p.nodes(), nodeIds({1, 2, 4})) << "A-B-D (cost 4) beats A-C-D (cost 6)";
    EXPECT_EQ(p.links(), linkIds({1, 2}));
    EXPECT_DOUBLE_EQ(p.cost(), 4.0);
    EXPECT_EQ(p.hopCount(), 2u);
    expectValidPath(t, p, A, D, CostMetric::Administrative);
}

TEST(Routing, CheaperLongerRouteBeatsExpensiveShortOne) {
    // 1-4 direct costs 10; 1-2-3-4 costs 3.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 4, 1, 10), routedLink(2, 1, 2, 1, 1),
                                                    routedLink(3, 2, 3, 1, 1), routedLink(4, 3, 4, 1, 1)});
    const Path p = must(RoutingEngine{}.findPath(t, A, D));
    EXPECT_EQ(p.nodes(), nodeIds({1, 2, 3, 4}));
    EXPECT_EQ(p.links(), linkIds({2, 3, 4}));
    EXPECT_DOUBLE_EQ(p.cost(), 3.0);
    EXPECT_EQ(p.hopCount(), 3u);
}

TEST(Routing, MultiHopPathIsReconstructedInOrderFromSourceToDestination) {
    // Links are registered "backwards" so ordering cannot come from insertion order.
    const Topology t = makeTopology({1, 2, 3, 4, 5}, {routedLink(4, 4, 5, 1, 1), routedLink(3, 3, 4, 1, 1),
                                                       routedLink(2, 2, 3, 1, 1), routedLink(1, 1, 2, 1, 1)});
    const Path p = must(RoutingEngine{}.findPath(t, NodeId{5}, NodeId{1}));
    EXPECT_EQ(p.nodes(), nodeIds({5, 4, 3, 2, 1}));
    EXPECT_EQ(p.links(), linkIds({4, 3, 2, 1}));
    EXPECT_EQ(p.hopCount(), 4u);
    expectValidPath(t, p, NodeId{5}, NodeId{1}, CostMetric::Administrative);
}

TEST(Routing, SourceEqualsDestinationIsTheTrivialPath) {
    const Topology t = specGraph();
    const Path p = must(RoutingEngine{}.findPath(t, B, B));
    EXPECT_TRUE(p.isTrivial());
    EXPECT_EQ(p.nodes(), nodeIds({2}));
    EXPECT_TRUE(p.links().empty());
    EXPECT_DOUBLE_EQ(p.cost(), 0.0);
    EXPECT_EQ(p.hopCount(), 0u);
}

TEST(Routing, SourceEqualsDestinationWorksForIsolatedNode) {
    const Topology t = makeTopology({1}, {});
    EXPECT_TRUE(must(RoutingEngine{}.findPath(t, A, A)).isTrivial());
}

TEST(Routing, UnreachableDestinationIsNoRouteNotACrash) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1)});
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, C), ErrorCode::NoRoute);
    ASSERT_ERROR(RoutingEngine{}.findPath(t, C, A), ErrorCode::NoRoute);
}

TEST(Routing, UnknownEndpointsAreNotFound) {
    const Topology t = specGraph();
    ASSERT_ERROR(RoutingEngine{}.findPath(t, NodeId{99}, A), ErrorCode::NotFound);
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, NodeId{99}), ErrorCode::NotFound);
    ASSERT_ERROR(RoutingEngine{}.findPath(t, NodeId{98}, NodeId{99}), ErrorCode::NotFound);
    ASSERT_ERROR(RoutingEngine{}.findPath(Topology{}, A, A), ErrorCode::NotFound) << "even source == destination";
}

TEST(Routing, ErrorMessagesNameTheNodes) {
    const Topology t = makeTopology({1, 2}, {});
    const auto r = RoutingEngine{}.findPath(t, A, B);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find('1'), std::string::npos);
    EXPECT_NE(r.error().message.find('2'), std::string::npos);
    EXPECT_EQ(toString(ErrorCode::NoRoute), "NoRoute");
    EXPECT_EQ(toString(ErrorCode::NoFeasibleRoute), "NoFeasibleRoute");
}

TEST(Routing, RoutingDoesNotModifyTheTopology) {
    const Topology t = specGraph();
    const std::string before = std::to_string(t.nodeCount()) + "/" + std::to_string(t.linkCount());
    (void)RoutingEngine{}.findPath(t, A, D);
    EXPECT_EQ(std::to_string(t.nodeCount()) + "/" + std::to_string(t.linkCount()), before);
    EXPECT_EQ(t.degree(A), 2u);
}

// ---------------------------------------------------------------- cost metrics

TEST(RoutingMetrics, HopCountPicksTheFewestLinks) {
    const Topology t = metricGraph();
    const Path p = must(RoutingEngine{}.findPath(t, A, D, withMetric(CostMetric::HopCount)));
    EXPECT_EQ(p.nodes(), nodeIds({1, 4}));
    EXPECT_DOUBLE_EQ(p.cost(), 1.0);
    EXPECT_EQ(p.metric(), CostMetric::HopCount);
    expectValidPath(t, p, A, D, CostMetric::HopCount);
}

TEST(RoutingMetrics, DistancePicksTheShortestPhysicalRoute) {
    const Topology t = metricGraph();
    const Path p = must(RoutingEngine{}.findPath(t, A, D, withMetric(CostMetric::Distance)));
    EXPECT_EQ(p.nodes(), nodeIds({1, 2, 4}));
    EXPECT_DOUBLE_EQ(p.cost(), 60.0);
    expectValidPath(t, p, A, D, CostMetric::Distance);
}

TEST(RoutingMetrics, AdministrativeCostPicksTheOperatorPreferredRoute) {
    const Topology t = metricGraph();
    const Path p = must(RoutingEngine{}.findPath(t, A, D, withMetric(CostMetric::Administrative)));
    EXPECT_EQ(p.nodes(), nodeIds({1, 3, 4}));
    EXPECT_DOUBLE_EQ(p.cost(), 2.0);
    expectValidPath(t, p, A, D, CostMetric::Administrative);
}

TEST(RoutingMetrics, DefaultMetricIsAdministrative) {
    const Topology t = metricGraph();
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, D)).nodes(), nodeIds({1, 3, 4}));
    EXPECT_EQ(RoutingConstraints{}.metric, CostMetric::Administrative);
}

TEST(RoutingMetrics, SameTopologyGivesDifferentAnswersWithoutRebuilding) {
    const Topology t = metricGraph();
    const RoutingEngine engine;
    EXPECT_NE(must(engine.findPath(t, A, D, withMetric(CostMetric::Distance))),
              must(engine.findPath(t, A, D, withMetric(CostMetric::Administrative))));
}

// ------------------------------------------------------------ directed links

TEST(RoutingDirection, DirectedLinkAllowsForwardTraversal) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Directed)});
    const Path p = must(RoutingEngine{}.findPath(t, A, B));
    EXPECT_EQ(p.links(), linkIds({1}));
}

TEST(RoutingDirection, DirectedLinkBlocksReverseTraversal) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Directed)});
    ASSERT_ERROR(RoutingEngine{}.findPath(t, B, A), ErrorCode::NoRoute);
}

TEST(RoutingDirection, ReverseWorksWhenAnotherLinkProvidesIt) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Directed),
                                              routedLink(2, 2, 1, 1, 5, LinkDirection::Directed)});
    const Path p = must(RoutingEngine{}.findPath(t, B, A));
    EXPECT_EQ(p.links(), linkIds({2}));
    EXPECT_DOUBLE_EQ(p.cost(), 5.0);
}

TEST(RoutingDirection, DirectedEdgeBlocksAnOtherwisePossibleRoute) {
    // 1 -> 2 is one-way, so 2 cannot reach 1 over the short route and must go around 2-3-1.
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Directed), routedLink(2, 2, 3, 1, 4),
                                                routedLink(3, 3, 1, 1, 4)});
    const Path forward = must(RoutingEngine{}.findPath(t, A, B));
    EXPECT_EQ(forward.nodes(), nodeIds({1, 2}));
    const Path back = must(RoutingEngine{}.findPath(t, B, A));
    EXPECT_EQ(back.nodes(), nodeIds({2, 3, 1}));
    EXPECT_DOUBLE_EQ(back.cost(), 8.0);
    expectValidPath(t, back, B, A, CostMetric::Administrative);
}

TEST(RoutingDirection, MixedTopologyRespectsEachLink) {
    // 1 <-> 2 (bidirectional), 2 -> 3 (directed)
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1, LinkDirection::Directed)});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, C)).nodes(), nodeIds({1, 2, 3}));
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, B, A)).nodes(), nodeIds({2, 1}));
    ASSERT_ERROR(RoutingEngine{}.findPath(t, C, A), ErrorCode::NoRoute);
    ASSERT_ERROR(RoutingEngine{}.findPath(t, C, B), ErrorCode::NoRoute);
}

// ------------------------------------------------------------- parallel links

TEST(RoutingParallel, CheapestParallelLinkIsChosen) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 10, 10), routedLink(2, 1, 2, 5, 5)});
    const Path p = must(RoutingEngine{}.findPath(t, A, B));
    EXPECT_EQ(p.links(), linkIds({2}));
    EXPECT_DOUBLE_EQ(p.cost(), 5.0);
}

TEST(RoutingParallel, ChoiceFollowsTheMetricPerLink) {
    // Link 1 is shorter, link 2 has the lower administrative cost.
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 5, 10), routedLink(2, 1, 2, 10, 5)});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, B, withMetric(CostMetric::Distance))).links(), linkIds({1}));
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, B, withMetric(CostMetric::Administrative))).links(), linkIds({2}));
}

TEST(RoutingParallel, CheapestParallelLinkOnEachHop) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 9), routedLink(2, 1, 2, 1, 2), routedLink(3, 2, 3, 1, 7),
                                                routedLink(4, 2, 3, 1, 3)});
    const Path p = must(RoutingEngine{}.findPath(t, A, C));
    EXPECT_EQ(p.nodes(), nodeIds({1, 2, 3}));
    EXPECT_EQ(p.links(), linkIds({2, 4}));
    EXPECT_DOUBLE_EQ(p.cost(), 5.0);
}

TEST(RoutingParallel, ParallelLinksOfDifferentDirectionAreEvaluatedIndependently) {
    // The cheap link is one-way A->B; going B->A must use the dearer bidirectional one.
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Directed), routedLink(2, 1, 2, 1, 6)});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, B)).links(), linkIds({1}));
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, B, A)).links(), linkIds({2}));
}

// ---------------------------------------------------------------- determinism

TEST(RoutingDeterminism, EqualCostParallelLinksChooseTheSmallerLinkId) {
    const Topology t = makeTopology({1, 2}, {routedLink(9, 1, 2, 1, 4), routedLink(3, 1, 2, 1, 4), routedLink(6, 1, 2, 1, 4)});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, B)).links(), linkIds({3}));
}

TEST(RoutingDeterminism, EqualCostBranchesChooseTheSmallerPredecessorNode) {
    // Diamond: A-B-D and A-C-D both cost 6 and use 2 hops.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 3), routedLink(2, 2, 4, 1, 3), routedLink(3, 1, 3, 1, 3),
                                                   routedLink(4, 3, 4, 1, 3)});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, D)).nodes(), nodeIds({1, 2, 4}));
}

TEST(RoutingDeterminism, EqualCostPrefersFewerHops) {
    // direct A-D costs 4; A-B-C-D costs 1+1+2 = 4 as well.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1), routedLink(3, 3, 4, 1, 2),
                                                   routedLink(4, 1, 4, 1, 4)});
    const Path p = must(RoutingEngine{}.findPath(t, A, D));
    EXPECT_EQ(p.links(), linkIds({4}));
    EXPECT_EQ(p.hopCount(), 1u);
}

TEST(RoutingDeterminism, LateFewerHopArrivalReplacesAnEarlierEqualCostOne) {
    // D is first reached via 1-2-3-D (cost 4, 3 hops) because node 3 settles early (cost 2),
    // and later via 1-Y-D (cost 4, 2 hops) from node 5, which settles at cost 3. The rule
    // "equal cost: fewer hops" must pick the later, shorter one.
    const Topology t = makeTopology({1, 2, 3, 4, 5}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1), routedLink(3, 3, 4, 1, 2),
                                                      routedLink(4, 1, 5, 1, 3), routedLink(5, 5, 4, 1, 1)});
    const Path p = must(RoutingEngine{}.findPath(t, A, D));
    EXPECT_DOUBLE_EQ(p.cost(), 4.0);
    EXPECT_EQ(p.nodes(), nodeIds({1, 5, 4}));
    EXPECT_EQ(p.hopCount(), 2u);
}

TEST(RoutingDeterminism, EqualCostEqualHopsPrefersTheSmallerPredecessorEvenIfItSettlesLater) {
    // Both routes to D cost 6 and use 2 hops. Via node 3 the prefix is cheaper (settles first),
    // via node 2 the prefix is dearer (settles later). The smaller predecessor id (2) must win.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 3, 1, 2), routedLink(2, 3, 4, 1, 4), routedLink(3, 1, 2, 1, 5),
                                                   routedLink(4, 2, 4, 1, 1)});
    const Path p = must(RoutingEngine{}.findPath(t, A, D));
    EXPECT_DOUBLE_EQ(p.cost(), 6.0);
    EXPECT_EQ(p.nodes(), nodeIds({1, 2, 4}));
    EXPECT_EQ(p.links(), linkIds({3, 4}));
}

TEST(RoutingDeterminism, EqualCostEqualHopsSamePredecessorPicksTheSmallerLink) {
    // Two routes through node 2 to D with equal cost: parallel links 2-4 (ids 8 and 5, same cost).
    const Topology t = makeTopology({1, 2, 4}, {routedLink(1, 1, 2, 1, 1), routedLink(8, 2, 4, 1, 3), routedLink(5, 2, 4, 1, 3)});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, D)).links(), linkIds({1, 5}));
}

TEST(RoutingDeterminism, ResultDoesNotDependOnLinkInsertionOrder) {
    std::vector<FiberLink> links = {routedLink(1, 1, 2, 1, 3), routedLink(2, 2, 4, 1, 3), routedLink(3, 1, 3, 1, 3),
                                    routedLink(4, 3, 4, 1, 3), routedLink(5, 1, 4, 1, 6), routedLink(6, 1, 4, 1, 6)};
    std::optional<Path> reference;
    for (int rotation = 0; rotation < 6; ++rotation) {
        Topology t;
        for (std::uint32_t id : {4u, 3u, 2u, 1u}) ASSERT_OK(t.addNode(makeNode(id)));
        for (std::size_t i = 0; i < links.size(); ++i) ASSERT_OK(t.addLink(links[(i + static_cast<std::size_t>(rotation)) % links.size()]));
        const Path p = must(RoutingEngine{}.findPath(t, A, D));
        if (!reference) reference = p;
        EXPECT_EQ(p, *reference) << "rotation " << rotation;
    }
    EXPECT_EQ(reference->links(), linkIds({5})) << "6 = 6 = 6, fewest hops (1) wins, then smallest LinkId";
}

TEST(RoutingDeterminism, RepeatedRunsGiveIdenticalResults) {
    // 4x4 grid: many equal-cost shortest paths between opposite corners.
    Topology t;
    for (std::uint32_t i = 1; i <= 16; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    std::uint32_t id = 1;
    for (std::uint32_t r = 0; r < 4; ++r) {
        for (std::uint32_t c = 0; c < 4; ++c) {
            const std::uint32_t n = r * 4 + c + 1;
            if (c < 3) ASSERT_OK(t.addLink(routedLink(id++, n, n + 1, 1, 1)));
            if (r < 3) ASSERT_OK(t.addLink(routedLink(id++, n, n + 4, 1, 1)));
        }
    }
    const Path first = must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{16}));
    EXPECT_DOUBLE_EQ(first.cost(), 6.0);
    for (int i = 0; i < 25; ++i) EXPECT_EQ(must(RoutingEngine{}.findPath(t, NodeId{1}, NodeId{16})), first);
}

// --------------------------------------------------------------------- engine

namespace {

class FixedRouter final : public IRoutingAlgorithm {
public:
    Result<Path> findPath(const Topology&, NodeId, NodeId, const RoutingConstraints&) const override {
        return Error{ErrorCode::NoRoute, "fixed"};
    }
};

}  // namespace

TEST(RoutingEngine, DelegatesToTheConfiguredAlgorithm) {
    const RoutingEngine engine{std::make_unique<FixedRouter>()};
    const Topology t = specGraph();
    const auto r = engine.findPath(t, A, D);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "fixed");
}

TEST(RoutingEngine, NullAlgorithmIsAProgrammingError) {
    EXPECT_THROW(RoutingEngine{std::unique_ptr<IRoutingAlgorithm>{}}, std::invalid_argument);
}

TEST(RoutingEngine, DijkstraRouterCanBeUsedDirectly) {
    const Topology t = specGraph();
    const DijkstraRouter router;
    const IRoutingAlgorithm& algorithm = router;
    EXPECT_EQ(must(algorithm.findPath(t, A, D, {})).nodes(), nodeIds({1, 2, 4}));
}

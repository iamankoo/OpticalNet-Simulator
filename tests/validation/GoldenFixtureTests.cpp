// Hand-computed reference results for the small fixtures in tests/validation/fixtures/*.json. If production behaviour
// changes, these exact numbers make the regression obvious.
#include "ValidationTestUtils.hpp"
#include "opticalnet/routing/DijkstraRouter.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

Path route(const Topology& t, std::uint32_t s, std::uint32_t d, CostMetric metric, const RoutingConstraints* base = nullptr) {
    RoutingConstraints c = base ? *base : RoutingConstraints{};
    c.metric = metric;
    const auto path = DijkstraRouter{}.findPath(t, NodeId{s}, NodeId{d}, c);
    EXPECT_TRUE(path.ok()) << (path.ok() ? "" : path.error().describe());
    if (!path.ok()) return must(Path::create(nodeIds({s}), {}, 0.0, metric));
    EXPECT_VALID(checkPath(t, path.value(), PathExpectation{NodeId{s}, NodeId{d}, metric, &c, nullptr}));
    return path.value();
}

}  // namespace

TEST(ValGolden, SimpleLine) {  // A-B 10km/1, B-C 20km/2, C-D 30km/3
    const Topology t = fixture("simple_line");
    const Path p = route(t, 1, 4, CostMetric::Administrative);
    EXPECT_EQ(p.nodes(), nodeIds({1, 2, 3, 4}));
    EXPECT_EQ(p.links(), linkIds({1, 2, 3}));
    EXPECT_DOUBLE_EQ(p.cost(), 6.0);
    EXPECT_DOUBLE_EQ(route(t, 1, 4, CostMetric::Distance).cost(), 60.0);
    EXPECT_DOUBLE_EQ(route(t, 1, 4, CostMetric::HopCount).cost(), 3.0);
    EXPECT_EQ(route(t, 4, 2, CostMetric::Administrative).links(), linkIds({3, 2})) << "bidirectional: usable backwards";
    EXPECT_DOUBLE_EQ(route(t, 2, 4, CostMetric::Distance).cost(), 50.0);
}

TEST(ValGolden, TriangleMetricsPickDifferentRoutes) {  // A-B 10km/5, B-C 10km/5, A-C 25km/20
    const Topology t = fixture("triangle");
    EXPECT_EQ(route(t, 1, 3, CostMetric::HopCount).links(), linkIds({3})) << "fewest hops: the direct link";
    EXPECT_EQ(route(t, 1, 3, CostMetric::Distance).links(), linkIds({1, 2})) << "20 km beats 25 km";
    EXPECT_DOUBLE_EQ(route(t, 1, 3, CostMetric::Distance).cost(), 20.0);
    EXPECT_EQ(route(t, 1, 3, CostMetric::Administrative).links(), linkIds({1, 2})) << "cost 10 beats 20";
    EXPECT_DOUBLE_EQ(route(t, 1, 3, CostMetric::Administrative).cost(), 10.0);
}

TEST(ValGolden, RingTieBreakingAndShortSide) {  // 6 nodes, link i joins i and i%6+1
    const Topology t = fixture("ring");
    EXPECT_EQ(route(t, 1, 3, CostMetric::HopCount).nodes(), nodeIds({1, 2, 3}));
    EXPECT_EQ(route(t, 1, 4, CostMetric::HopCount).nodes(), nodeIds({1, 2, 3, 4})) << "3 hops either way: smaller predecessor id wins";
    EXPECT_EQ(route(t, 1, 5, CostMetric::HopCount).nodes(), nodeIds({1, 6, 5})) << "2 hops the short way round";
    EXPECT_EQ(route(t, 1, 5, CostMetric::HopCount).links(), linkIds({6, 5}));
}

TEST(ValGolden, ParallelLinksKeepTheirIdentity) {  // p1 10km/5, p2 5km/9, p3 7km/5
    const Topology t = fixture("parallel_links");
    EXPECT_EQ(route(t, 1, 2, CostMetric::Distance).links(), linkIds({2})) << "shortest fiber";
    EXPECT_EQ(route(t, 1, 2, CostMetric::Administrative).links(), linkIds({1})) << "p1 and p3 tie at cost 5: smaller LinkId";
    EXPECT_EQ(route(t, 1, 2, CostMetric::HopCount).links(), linkIds({1}));
    RoutingConstraints c;
    c.blockedLinks.insert(LinkId{1});
    EXPECT_EQ(route(t, 1, 2, CostMetric::Administrative, &c).links(), linkIds({3}));
    c.blockedLinks.insert(LinkId{3});
    EXPECT_EQ(route(t, 1, 2, CostMetric::Administrative, &c).links(), linkIds({2}));
}

TEST(ValGolden, DirectedGraphFollowsTheArrows) {  // 1->2, 2->3, 3->1
    const Topology t = fixture("directed_graph");
    EXPECT_EQ(route(t, 1, 2, CostMetric::HopCount).nodes(), nodeIds({1, 2}));
    EXPECT_EQ(route(t, 2, 1, CostMetric::HopCount).nodes(), nodeIds({2, 3, 1})) << "the long way: there is no reverse link";
    EXPECT_EQ(route(t, 3, 2, CostMetric::HopCount).nodes(), nodeIds({3, 1, 2}));
    EXPECT_EQ(route(t, 1, 3, CostMetric::HopCount).links(), linkIds({1, 2}));
    EXPECT_FALSE(exhaustiveRoute(t, NodeId{1}, NodeId{2}, {}).hops == 2) << "1 -> 2 is a single hop";
}

TEST(ValGolden, CapacityCongestionSequence) {  // top A-B-D (cap 4, cost 2), bottom A-C-D (cap 10, cost 4)
    const Topology t = fixture("capacity_congestion");
    ConnectionManager m{t};
    const auto req = [](std::uint32_t id, std::uint32_t demand) { return ConnectionRequest{ConnectionId{id}, NodeId{1}, NodeId{4}, demand, {}}; };
    EXPECT_EQ(must(m.establish(req(1, 3))).allocatedLinks(), linkIds({1, 2}));
    EXPECT_EQ(must(m.establish(req(2, 3))).allocatedLinks(), linkIds({3, 4}));
    EXPECT_EQ(must(m.establish(req(3, 5))).allocatedLinks(), linkIds({3, 4}));
    ASSERT_ERROR(m.establish(req(4, 5)), ErrorCode::InsufficientCapacity);
    ASSERT_OK(m.release(ConnectionId{2}));
    EXPECT_EQ(must(m.establish(req(4, 5))).allocatedLinks(), linkIds({3, 4})) << "released capacity is reusable";
    EXPECT_VALID(checkConnections(m, t));
    EXPECT_EQ(m.resources().totalAllocated(), 6u + 10u + 10u);
}

TEST(ValGolden, LinkFailureReroute) {  // square: A-B-C cost 2, A-D-C cost 4
    const Topology t = fixture("link_failure");
    FailureState f(t);
    RoutingConstraints healthy;
    EXPECT_EQ(route(t, 1, 3, CostMetric::Administrative).nodes(), nodeIds({1, 2, 3}));
    ASSERT_OK(f.failLink(LinkId{2}));
    RoutingConstraints damaged;
    damaged.linkFilter = [&f](const FiberLink& l) { return f.isLinkUsable(l); };
    EXPECT_EQ(route(t, 1, 3, CostMetric::Administrative, &damaged).nodes(), nodeIds({1, 4, 3}));
    EXPECT_DOUBLE_EQ(route(t, 1, 3, CostMetric::Administrative, &damaged).cost(), 4.0);
    ASSERT_OK(f.recoverLink(LinkId{2}));
    EXPECT_EQ(route(t, 1, 3, CostMetric::Administrative, &damaged).nodes(), nodeIds({1, 2, 3}));
}

TEST(ValGolden, NodeFailureDetour) {  // A-B-C (2 hops), A-D-E-C (3 hops)
    const Topology t = fixture("node_failure");
    FailureState f(t);
    RoutingConstraints damaged;
    damaged.linkFilter = [&f](const FiberLink& l) { return f.isLinkUsable(l); };
    EXPECT_EQ(route(t, 1, 3, CostMetric::HopCount, &damaged).nodes(), nodeIds({1, 2, 3}));
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_EQ(route(t, 1, 3, CostMetric::HopCount, &damaged).nodes(), nodeIds({1, 4, 5, 3}));
    ASSERT_OK(f.failLink(LinkId{4}));
    const auto none = DijkstraRouter{}.findPath(t, NodeId{1}, NodeId{3}, damaged);
    ASSERT_FALSE(none.ok());
    EXPECT_EQ(none.error().code, ErrorCode::NoFeasibleRoute) << "every route is cut, but the network itself is connected";
}

TEST(ValGolden, CapacityConstrainedReroute) {  // detour A-D-C holds only 4 channels
    const Topology t = fixture("reroute");
    const auto r = runScript(t, {scripted(1, 1, 1, 3, 4, 100), scripted(2, 2, 1, 3, 4, 100)}, tracedConfig(10, {{5, FailureAction::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsAffected, 2u);
    EXPECT_EQ(r.metrics.connectionsRerouted, 1u);
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 8u) << "connection 1 moved to the 2-hop detour with 4 channels";
    EXPECT_DOUBLE_EQ(r.metrics.recoverySuccessRate, 0.5);
    ASSERT_VALID(checkSimulationTrace(t, r));
}

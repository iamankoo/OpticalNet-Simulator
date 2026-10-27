// The validators are only worth something if they notice real problems. These tests feed them deliberately wrong
// data (fabricated paths, tampered results, inconsistent state) and check that the right check fires, and that
// correct data passes.
#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

Path pathOf(std::initializer_list<std::uint32_t> nodes, std::initializer_list<std::uint32_t> links, double cost,
            CostMetric metric = CostMetric::Administrative) {
    return must(Path::create(nodeIds(nodes), linkIds(links), cost, metric));
}

PathExpectation expect(std::uint32_t from, std::uint32_t to, CostMetric metric = CostMetric::Administrative,
                       const RoutingConstraints* constraints = nullptr, const FailureState* failures = nullptr) {
    return PathExpectation{NodeId{from}, NodeId{to}, metric, constraints, failures};
}

// A small traced run exercising arrivals, a release, a failure, a reroute.
SimulationResult sampleRun(const Topology& t) {
    return runScript(t, {scripted(1, 1, 1, 3, 2, 100), scripted(2, 2, 4, 2, 3, 3), scripted(2.5, 3, 1, 2, 99, 5)},
                     tracedConfig(10, {{3, FailureAction::FailLink, 2}}));
}

}  // namespace

// ============================================================================ ValidationResult

TEST(ValSelfResult, PassMergeAndDescribe) {
    ValidationResult a;
    EXPECT_TRUE(a.passed());
    EXPECT_EQ(a.describe(), "");
    a.fail("x.one", "first problem");
    ValidationResult b;
    b.fail("x.two", "second problem");
    a.merge(b);
    EXPECT_FALSE(a.passed());
    EXPECT_EQ(a.failures().size(), 2u);
    EXPECT_TRUE(a.failed("x.one"));
    EXPECT_TRUE(a.failed("x.two"));
    EXPECT_FALSE(a.failed("x.three"));
    EXPECT_NE(a.describe().find("first problem"), std::string::npos);
    EXPECT_NE(a.describe(1).find("1 more"), std::string::npos);
}

// ============================================================================ checkPath

TEST(ValSelfPath, AcceptsACorrectPath) {
    const Topology t = fixture("simple_line");
    EXPECT_VALID(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 3.0), expect(1, 3)));
    EXPECT_VALID(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 30.0, CostMetric::Distance), expect(1, 3, CostMetric::Distance)));
    EXPECT_VALID(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 2.0, CostMetric::HopCount), expect(1, 3, CostMetric::HopCount)));
}

TEST(ValSelfPath, DetectsAWrongCost) {
    const Topology t = fixture("simple_line");
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 99.0), expect(1, 3)), "path.cost");
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 3.0, CostMetric::Distance), expect(1, 3, CostMetric::Distance)), "path.cost");
}

TEST(ValSelfPath, DetectsWrongEndpoints) {
    const Topology t = fixture("simple_line");
    const auto r = checkPath(t, pathOf({1, 2, 3}, {1, 2}, 3.0), expect(2, 4));
    EXPECT_DETECTED(r, "path.starts_at_source");
    EXPECT_DETECTED(r, "path.ends_at_destination");
}

TEST(ValSelfPath, DetectsMissingAndDisconnectedLinks) {
    const Topology t = fixture("simple_line");
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2, 3}, {1, 77}, 3.0), expect(1, 3)), "path.links_exist");
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2, 4}, {1, 3}, 4.0), expect(1, 4)), "path.links_connect_consecutive_nodes");
}

TEST(ValSelfPath, DetectsDirectedLinksUsedBackwards) {
    const Topology t = fixture("directed_graph");  // 1->2, 2->3, 3->1
    EXPECT_VALID(checkPath(t, pathOf({1, 2}, {1}, 1.0), expect(1, 2)));
    EXPECT_DETECTED(checkPath(t, pathOf({2, 1}, {1}, 1.0), expect(2, 1)), "path.links_connect_consecutive_nodes");
}

TEST(ValSelfPath, BidirectionalLinksMayBeUsedBothWays) {
    const Topology t = fixture("simple_line");
    EXPECT_VALID(checkPath(t, pathOf({3, 2, 1}, {2, 1}, 3.0), expect(3, 1)));
}

TEST(ValSelfPath, DetectsFailedLinksAndNodes) {
    const Topology t = fixture("simple_line");
    FailureState f(t);
    ASSERT_OK(f.failLink(LinkId{2}));
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 3.0), expect(1, 3, CostMetric::Administrative, nullptr, &f)), "path.avoids_failed_links");
    ASSERT_OK(f.recoverLink(LinkId{2}));
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 3.0), expect(1, 3, CostMetric::Administrative, nullptr, &f)), "path.avoids_failed_nodes");
    ASSERT_OK(f.recoverNode(NodeId{2}));
    EXPECT_VALID(checkPath(t, pathOf({1, 2, 3}, {1, 2}, 3.0), expect(1, 3, CostMetric::Administrative, nullptr, &f)));
}

TEST(ValSelfPath, DetectsConstraintViolations) {
    const Topology t = fixture("simple_line");
    const Path p = pathOf({1, 2, 3}, {1, 2}, 3.0);
    RoutingConstraints c;
    c.maxHops = 1;
    EXPECT_DETECTED(checkPath(t, p, expect(1, 3, CostMetric::Administrative, &c)), "path.max_hops");
    c = {};
    c.maxCost = 2.5;
    EXPECT_DETECTED(checkPath(t, p, expect(1, 3, CostMetric::Administrative, &c)), "path.max_cost");
    c = {};
    c.blockedNodes.insert(NodeId{2});
    EXPECT_DETECTED(checkPath(t, p, expect(1, 3, CostMetric::Administrative, &c)), "path.avoids_blocked_nodes");
    c = {};
    c.blockedLinks.insert(LinkId{1});
    EXPECT_DETECTED(checkPath(t, p, expect(1, 3, CostMetric::Administrative, &c)), "path.avoids_blocked_links");
    c = {};
    c.minLinkCapacityChannels = 11;
    EXPECT_DETECTED(checkPath(t, p, expect(1, 3, CostMetric::Administrative, &c)), "path.link_capacity");
    c = {};
    c.linkFilter = [](const FiberLink& l) { return l.id() != LinkId{2}; };
    EXPECT_DETECTED(checkPath(t, p, expect(1, 3, CostMetric::Administrative, &c)), "path.link_filter");
    c = {};
    c.maxHops = 2;
    c.maxCost = 3.0;  // exactly at the boundary: fine
    EXPECT_VALID(checkPath(t, p, expect(1, 3, CostMetric::Administrative, &c)));
}

TEST(ValSelfPath, DetectsRepeatedNodesAndMetricMismatch) {
    const Topology t = fixture("simple_line");
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2, 1}, {1, 1}, 2.0), expect(1, 1)), "path.simple");
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2}, {1}, 1.0, CostMetric::HopCount), expect(1, 2, CostMetric::Administrative)), "path.metric");
}

TEST(ValSelfPath, ParallelLinkIdentityIsPartOfThePath) {
    const Topology t = fixture("parallel_links");  // links 1, 2, 3 all join nodes 1 and 2
    EXPECT_VALID(checkPath(t, pathOf({1, 2}, {2}, 9.0), expect(1, 2)));
    EXPECT_DETECTED(checkPath(t, pathOf({1, 2}, {2}, 5.0), expect(1, 2)), "path.cost") << "link 2 costs 9, not link 1's 5";
}

// ============================================================================ checkTopology / failure state

TEST(ValSelfTopology, FixturesAndEmptyTopologyPass) {
    for (const char* name : {"simple_line", "triangle", "ring", "parallel_links", "directed_graph", "capacity_congestion", "link_failure",
                             "node_failure", "reroute"})
        EXPECT_VALID(checkTopology(fixture(name))) << name;
    EXPECT_VALID(checkTopology(Topology{}));
}

TEST(ValSelfTopology, DetectsPortConstraintViolations) {
    Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 1, 3, 1, 1)});
    ASSERT_OK(t.addSwitchingElement(makeSwitch(1, 1)));  // one port, node 1 has two links
    ASSERT_OK(t.attachSwitchingElement(NodeId{1}, SwitchingElementId{1}));
    EXPECT_DETECTED(checkTopology(t), "topology.port_constraint");
    EXPECT_VALID(checkTopology(t, /*checkPorts=*/false));
}

TEST(ValSelfFailures, FailureStateCheckPassesThroughAWalk) {
    const Topology t = fixture("node_failure");
    FailureState f(t);
    EXPECT_VALID(checkFailureState(f, t));
    ASSERT_OK(f.failLink(LinkId{3}));
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_VALID(checkFailureState(f, t));
    ASSERT_OK(f.recoverNode(NodeId{2}));
    ASSERT_OK(f.recoverLink(LinkId{3}));
    EXPECT_VALID(checkFailureState(f, t));
}

// ============================================================================ checkResources / checkConnections

TEST(ValSelfResources, ConsistentStatePassesAndOverAllocationIsDetected) {
    Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 50), routedLink(2, 1, 2, 1, 2, LinkDirection::Bidirectional, 50)});
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 30));
    EXPECT_VALID(checkResources(rm, t));
    // The link is replaced by a smaller one with the same id while 30 channels are allocated.
    ASSERT_OK(t.removeLink(LinkId{1}));
    ASSERT_OK(t.addLink(routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 10)));
    EXPECT_DETECTED(checkResources(rm, t), "resources.link_within_capacity");
}

TEST(ValSelfResources, AllocationOnAVanishedLinkIsDetected) {
    Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 1, 2, 1, 2)});
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({2}), 4));
    ASSERT_OK(t.removeLink(LinkId{2}));
    EXPECT_DETECTED(checkResources(rm, t), "resources.no_allocation_on_unknown_link");
}

TEST(ValSelfConnections, ConsistentManagerPassesAndBreakageIsDetected) {
    Topology t = fixture("link_failure");
    FailureState failures(t);
    ConnectionManager m{t};
    m.setAvailability([&failures](const FiberLink& l) { return failures.isLinkUsable(l); });
    ASSERT_OK(m.establish(ConnectionRequest{ConnectionId{1}, NodeId{1}, NodeId{3}, 2, {}}));
    EXPECT_VALID(checkConnections(m, t, &failures));
    EXPECT_VALID(checkResources(m.resources(), t));

    // A failure that nobody handled leaves a connection on a failed resource: the validator must say so.
    ASSERT_OK(failures.failLink(LinkId{2}));
    EXPECT_DETECTED(checkConnections(m, t, &failures), "connections.active_on_failed_resource");
    // Handling it (detach + re-establish) restores consistency.
    const Connection old = must(m.detach(ConnectionId{1}));
    ASSERT_OK(m.establish(old.request()));
    EXPECT_VALID(checkConnections(m, t, &failures));
}

TEST(ValSelfConnections, PathInvalidatedByTopologyChangeIsDetected) {
    Topology t = fixture("link_failure");
    ConnectionManager m{t};
    ASSERT_OK(m.establish(ConnectionRequest{ConnectionId{1}, NodeId{1}, NodeId{3}, 2, {}}));  // A-B-C over links 1 and 2
    ASSERT_OK(t.removeLink(LinkId{2}));
    ASSERT_OK(t.addLink(routedLink(2, 1, 4, 10, 1)));  // link 2 now joins A-D: the stored path no longer chains
    const auto r = checkConnections(m, t);
    EXPECT_DETECTED(r, "path.links_connect_consecutive_nodes");
}

// ============================================================================ checkSimulationTrace

TEST(ValSelfTrace, AnUntamperedRunPasses) {
    const Topology t = fixture("link_failure");
    const auto r = sampleRun(t);
    ASSERT_GE(r.trace.size(), 6u);
    EXPECT_GT(r.metrics.connectionsAffected, 0u);
    EXPECT_VALID(checkSimulationTrace(t, r));
}

TEST(ValSelfTrace, DetectsTamperedMetrics) {
    const Topology t = fixture("link_failure");
    const auto base = sampleRun(t);
    const auto tamper = [&](auto mutate, const char* check) {
        SimulationResult r = base;
        mutate(r.metrics);
        EXPECT_DETECTED(checkSimulationTrace(t, r), check);
    };
    tamper([](SimulationMetrics& m) { m.acceptedRequests += 1; }, "metrics.acceptedRequests");
    tamper([](SimulationMetrics& m) { m.rejectedRequests += 1; }, "metrics.rejectedRequests");
    tamper([](SimulationMetrics& m) { m.totalReleases += 1; }, "metrics.totalReleases");
    tamper([](SimulationMetrics& m) { m.averageUtilization *= 1.1; }, "metrics.averageUtilization");
    tamper([](SimulationMetrics& m) { m.peakAllocatedChannels -= 1; }, "metrics.peakAllocatedChannels");
    tamper([](SimulationMetrics& m) { m.finalAllocatedChannels += 1; }, "metrics.finalAllocatedChannels");
    tamper([](SimulationMetrics& m) { m.activeConnectionsAtEnd += 1; }, "metrics.activeConnectionsAtEnd");
    tamper([](SimulationMetrics& m) { m.peakActiveConnections += 1; }, "metrics.peakActiveConnections");
    tamper([](SimulationMetrics& m) { m.connectionsRerouted += 1; }, "metrics.connectionsRerouted");
    tamper([](SimulationMetrics& m) { m.connectionsLost += 1; }, "metrics.connectionsLost");
    tamper([](SimulationMetrics& m) { m.linkFailures += 1; }, "metrics.linkFailures");
    tamper([](SimulationMetrics& m) { m.averageHopCount += 0.5; }, "metrics.averageHopCount");
    tamper([](SimulationMetrics& m) { m.maxHopCount += 1; }, "metrics.maxHopCount");
    tamper([](SimulationMetrics& m) { m.acceptanceRate += 0.01; }, "metrics.acceptanceRate");
    tamper([](SimulationMetrics& m) { m.rejectionsByReason[ErrorCode::NoRoute] += 3; }, "metrics.rejectionsByReason");
    tamper([](SimulationMetrics& m) { m.totalCapacityChannels += 1; }, "metrics.totalCapacityChannels");
}

TEST(ValSelfTrace, DetectsTamperedTraces) {
    const Topology t = fixture("link_failure");
    const auto base = sampleRun(t);

    SimulationResult backwards = base;
    std::swap(backwards.trace[1].time, backwards.trace[2].time);
    EXPECT_DETECTED(checkSimulationTrace(t, backwards), "trace.time_monotonic");

    SimulationResult duplicate = base;
    duplicate.trace.insert(duplicate.trace.begin() + 1, duplicate.trace.front());  // the first accepted arrival twice
    EXPECT_DETECTED(checkSimulationTrace(t, duplicate), "trace.unique_active_ids");

    SimulationResult noRelease = base;
    for (std::size_t i = 0; i < noRelease.trace.size(); ++i)
        if (noRelease.trace[i].kind == TraceKind::Event && noRelease.trace[i].type == EventType::ConnectionRelease) {
            noRelease.trace.erase(noRelease.trace.begin() + static_cast<long>(i));
            break;
        }
    EXPECT_FALSE(checkSimulationTrace(t, noRelease).passed()) << "a missing release must show up in the metrics";

    SimulationResult phantom = base;
    TraceEntry release = phantom.trace.back();
    release.kind = TraceKind::Event;
    release.type = EventType::ConnectionRelease;
    release.connection = ConnectionId{999};
    phantom.trace.push_back(release);
    EXPECT_DETECTED(checkSimulationTrace(t, phantom), "trace.release_of_active");

    SimulationResult untraced = base;
    untraced.trace.clear();
    EXPECT_DETECTED(checkSimulationTrace(t, untraced), "trace.recorded");
}

// ============================================================================ reference models

TEST(ValSelfReference, ExhaustiveRouteOnTheTriangle) {
    const Topology t = fixture("triangle");  // A-B 10km/5, B-C 10km/5, A-C 25km/20
    RoutingConstraints c;
    c.metric = CostMetric::HopCount;
    auto best = exhaustiveRoute(t, NodeId{1}, NodeId{3}, c);
    EXPECT_TRUE(best.found);
    EXPECT_DOUBLE_EQ(best.cost, 1.0);
    EXPECT_EQ(best.pathsConsidered, 2u);
    c.metric = CostMetric::Distance;
    EXPECT_DOUBLE_EQ(exhaustiveRoute(t, NodeId{1}, NodeId{3}, c).cost, 20.0);
    c.metric = CostMetric::Administrative;
    EXPECT_DOUBLE_EQ(exhaustiveRoute(t, NodeId{1}, NodeId{3}, c).cost, 10.0);
    c.maxCost = 9.0;
    EXPECT_FALSE(exhaustiveRoute(t, NodeId{1}, NodeId{3}, c).found);
    c = {};
    c.maxHops = 1;
    EXPECT_DOUBLE_EQ(exhaustiveRoute(t, NodeId{1}, NodeId{3}, c).cost, 20.0) << "the direct link, the dearer route";
    c = {};
    c.blockedNodes.insert(NodeId{2});
    EXPECT_DOUBLE_EQ(exhaustiveRoute(t, NodeId{1}, NodeId{3}, c).cost, 20.0);
    c = {};
    c.blockedLinks.insert(LinkId{3});
    EXPECT_DOUBLE_EQ(exhaustiveRoute(t, NodeId{1}, NodeId{3}, c).cost, 10.0);
}

TEST(ValSelfReference, ExhaustiveRouteHonoursDirectionAndFailures) {
    const Topology t = fixture("directed_graph");
    EXPECT_TRUE(exhaustiveRoute(t, NodeId{1}, NodeId{3}, {}).found) << "1 -> 2 -> 3";
    EXPECT_EQ(exhaustiveRoute(t, NodeId{1}, NodeId{3}, {}).hops, 2u);
    FailureState f(t);
    ASSERT_OK(f.failLink(LinkId{2}));
    EXPECT_FALSE(exhaustiveRoute(t, NodeId{1}, NodeId{3}, {}, &f).found);
    EXPECT_TRUE(reachableByAnyPath(t, NodeId{1}, NodeId{3}));
    EXPECT_TRUE(exhaustiveRoute(t, NodeId{2}, NodeId{2}, {}).found) << "trivial path";
    ASSERT_OK(f.recoverLink(LinkId{2}));
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_FALSE(exhaustiveRoute(t, NodeId{1}, NodeId{3}, {}, &f).found);
    EXPECT_FALSE(exhaustiveRoute(t, NodeId{2}, NodeId{2}, {}, &f).found) << "a failed node cannot be a trivial endpoint either";
}

TEST(ValSelfReference, ShadowLedgerIsAllOrNothing) {
    const Topology t = fixture("capacity_congestion");  // l1,l2 cap 4; l3,l4 cap 10
    ShadowLedger ledger(t);
    EXPECT_TRUE(ledger.allocate(linkIds({1, 2}), 3));
    EXPECT_FALSE(ledger.allocate(linkIds({1, 2}), 2)) << "only 1 free";
    EXPECT_EQ(ledger.allocated(LinkId{1}), 3u) << "failed attempt changed nothing";
    EXPECT_FALSE(ledger.allocate(linkIds({3, 1}), 2));
    EXPECT_EQ(ledger.allocated(LinkId{3}), 0u);
    EXPECT_FALSE(ledger.allocate(linkIds({99}), 1));
    EXPECT_FALSE(ledger.allocate({}, 1));
    EXPECT_FALSE(ledger.allocate(linkIds({1}), 0));
    EXPECT_FALSE(ledger.allocate(linkIds({1, 1}), 1)) << "a repeated link needs its demand twice: 3 + 2 > 4";
    EXPECT_FALSE(ledger.release(linkIds({1, 3}), 1)) << "link 3 holds nothing";
    EXPECT_TRUE(ledger.release(linkIds({1, 2}), 3));
    EXPECT_EQ(ledger.totalAllocated(), 0u);
    EXPECT_TRUE(ledger.counters().empty());
}

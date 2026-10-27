#include <gtest/gtest.h>

#include <map>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/simulation/SimulationEngine.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

//   A(1) --l1-- B(2) --l2-- C(3)       primary A-C: A-B-C, 2 hops, cost 2
//    \                      /
//     l3 -- D(4) -l4- E(5) -l5         detour  A-C: A-D-E-C, 3 hops, cost 3
Topology detour(std::uint32_t channels = 10) {
    const auto bi = LinkDirection::Bidirectional;
    return makeTopology({1, 2, 3, 4, 5}, {routedLink(1, 1, 2, 1, 1, bi, channels), routedLink(2, 2, 3, 1, 1, bi, channels),
                                          routedLink(3, 1, 4, 1, 1, bi, channels), routedLink(4, 4, 5, 1, 1, bi, channels),
                                          routedLink(5, 5, 3, 1, 1, bi, channels)});
}

ScriptedArrival arrive(double time, std::uint32_t id, std::uint32_t from, std::uint32_t to, std::uint32_t demand, double lifetime,
                       RoutingConstraints constraints = {}) {
    return ScriptedArrival{time, ConnectionRequest{ConnectionId{id}, NodeId{from}, NodeId{to}, demand, std::move(constraints)}, lifetime};
}

SimulationConfig config(double endTime, std::vector<ScriptedFailure> failures, RecoveryPolicy policy = RecoveryPolicy::Reroute) {
    SimulationConfig c;
    c.endTime = endTime;
    c.recordTrace = true;
    c.failureSchedule = std::move(failures);
    c.recoveryPolicy = policy;
    return c;
}

SimulationResult runScripted(const Topology& t, const std::vector<ScriptedArrival>& script, const SimulationConfig& c) {
    return must(SimulationEngine{}.runScript(t, script, c));
}

using FA = FailureAction;

// Final allocation recomputed independently from the trace: arrivals, reroutes, losses and releases.
struct TraceSummary {
    std::uint64_t channelLinks = 0;
    std::size_t active = 0;
};
TraceSummary summarize(const SimulationResult& r) {
    std::map<ConnectionId, std::uint64_t> held;
    for (const auto& e : r.trace) {
        if (e.kind == TraceKind::Event && e.type == EventType::ConnectionArrival && e.success)
            held[e.connection] = static_cast<std::uint64_t>(e.capacityChannels) * e.hops;
        else if (e.kind == TraceKind::Event && e.type == EventType::ConnectionRelease)
            EXPECT_EQ(held.erase(e.connection), 1u);
        else if (e.kind == TraceKind::Rerouted)
            held[e.connection] = static_cast<std::uint64_t>(e.capacityChannels) * e.hops;
        else if (e.kind == TraceKind::Lost)
            EXPECT_EQ(held.erase(e.connection), 1u);
    }
    TraceSummary s;
    for (const auto& [id, channels] : held) s.channelLinks += channels;
    s.active = held.size();
    return s;
}

void expectConsistent(const SimulationResult& r) {
    const auto s = summarize(r);
    EXPECT_EQ(s.channelLinks, r.metrics.finalAllocatedChannels);
    EXPECT_EQ(s.active, r.metrics.activeConnectionsAtEnd);
    EXPECT_EQ(r.metrics.connectionsAffected, r.metrics.connectionsRerouted + r.metrics.connectionsLost);
    EXPECT_EQ(r.metrics.acceptedRequests, r.metrics.activeConnectionsAtEnd + r.metrics.totalReleases + r.metrics.connectionsLost);
    EXPECT_EQ(r.metrics.finalAllocatedChannels + r.metrics.finalAvailableChannels, r.metrics.totalCapacityChannels);
    EXPECT_EQ(r.metrics.totalFailureEvents, r.metrics.linkFailures + r.metrics.nodeFailures);
    EXPECT_EQ(r.metrics.failedLinksAtEnd, r.metrics.linkFailures - r.metrics.linkRecoveries);
    EXPECT_EQ(r.metrics.failedNodesAtEnd, r.metrics.nodeFailures - r.metrics.nodeRecoveries);
}

std::vector<TraceEntry> entriesOf(const SimulationResult& r, TraceKind kind) {
    std::vector<TraceEntry> out;
    for (const auto& e : r.trace)
        if (e.kind == kind) out.push_back(e);
    return out;
}

}  // namespace

// ============================================================ Fixture A: link failure, alternate route

TEST(FailureFixtureA, LinkFailureReroutesOverTheDetourKeepingIdAndDemand) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 4, 100)}, config(10, {{5, FA::FailLink, 2}}));
    const SimulationMetrics& m = r.metrics;

    EXPECT_EQ(m.acceptedRequests, 1u);
    EXPECT_EQ(m.totalFailureEvents, 1u);
    EXPECT_EQ(m.linkFailures, 1u);
    EXPECT_EQ(m.connectionsAffected, 1u);
    EXPECT_EQ(m.connectionsRerouted, 1u);
    EXPECT_EQ(m.connectionsLost, 0u);
    EXPECT_DOUBLE_EQ(m.recoverySuccessRate, 1.0);
    EXPECT_DOUBLE_EQ(m.totalConnectionDowntime, 0.0);
    EXPECT_EQ(m.activeConnectionsAtEnd, 1u) << "the rerouted connection is still active";
    EXPECT_EQ(m.finalAllocatedChannels, 12u) << "demand 4 over the 3-hop detour";
    EXPECT_EQ(m.failedLinksAtEnd, 1u);
    // Allocated: 8 channel-links on [1,5) (2 hops), 12 on [5,10] (3 hops); capacity 50:
    // (8*4 + 12*5) / (50*10) = 92/500
    EXPECT_DOUBLE_EQ(m.averageUtilization, 92.0 / 500.0);
    EXPECT_EQ(m.peakAllocatedChannels, 12u);
    EXPECT_DOUBLE_EQ(m.averageHopCount, 2.0) << "path statistics describe the first path";

    ASSERT_EQ(r.trace.size(), 4u);
    EXPECT_EQ(r.trace[0].kind, TraceKind::Event);
    EXPECT_EQ(r.trace[0].type, EventType::ConnectionArrival);
    EXPECT_EQ(r.trace[1].kind, TraceKind::Event);
    EXPECT_EQ(r.trace[1].type, EventType::LinkFailure);
    EXPECT_EQ(r.trace[1].resource, 2u);
    EXPECT_DOUBLE_EQ(r.trace[1].time, 5.0);
    EXPECT_EQ(r.trace[2].kind, TraceKind::Affected);
    EXPECT_EQ(r.trace[2].connection, ConnectionId{1});
    EXPECT_EQ(r.trace[2].hops, 2u) << "the old path";
    EXPECT_EQ(r.trace[3].kind, TraceKind::Rerouted);
    EXPECT_EQ(r.trace[3].connection, ConnectionId{1}) << "same ConnectionId";
    EXPECT_EQ(r.trace[3].capacityChannels, 4u) << "same demand";
    EXPECT_EQ(r.trace[3].hops, 3u) << "the detour";
    EXPECT_TRUE(r.trace[3].success);
    expectConsistent(r);
}

TEST(FailureFixtureA, UnaffectedConnectionsAreLeftAlone) {
    const Topology t = detour();
    // #1 uses A-B-C, #2 uses only A-D (l3). Failing l2 must touch #1 only.
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 2, 100), arrive(2, 2, 1, 4, 2, 100)}, config(10, {{5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsAffected, 1u);
    const auto affected = entriesOf(r, TraceKind::Affected);
    ASSERT_EQ(affected.size(), 1u);
    EXPECT_EQ(affected[0].connection, ConnectionId{1});
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 2u);
    expectConsistent(r);
}

TEST(FailureFixtureA, FailureOfAnUnusedLinkAffectsNobody) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 4, 100)}, config(10, {{5, FA::FailLink, 4}}));
    EXPECT_EQ(r.metrics.totalFailureEvents, 1u);
    EXPECT_EQ(r.metrics.connectionsAffected, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.recoverySuccessRate, 0.0) << "no division by zero";
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 8u);
    expectConsistent(r);
}

// ============================================================ Fixture B: no alternate route

TEST(FailureFixtureB, ConnectionIsLostWithoutLeakingResources) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1)});
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 4, 100)}, config(10, {{5, FA::FailLink, 2}}));
    const SimulationMetrics& m = r.metrics;
    EXPECT_EQ(m.connectionsAffected, 1u);
    EXPECT_EQ(m.connectionsRerouted, 0u);
    EXPECT_EQ(m.connectionsLost, 1u);
    EXPECT_DOUBLE_EQ(m.recoverySuccessRate, 0.0);
    EXPECT_EQ(m.activeConnectionsAtEnd, 0u);
    EXPECT_EQ(m.finalAllocatedChannels, 0u) << "the healthy link l1 must not keep the lost connection's channels";
    EXPECT_EQ(m.finalAvailableChannels, m.totalCapacityChannels);
    // The connection would have lasted until t=101; the run ends at 10, so it was down for 10 - 5.
    EXPECT_DOUBLE_EQ(m.totalConnectionDowntime, 5.0);
    const auto lost = entriesOf(r, TraceKind::Lost);
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_FALSE(lost[0].success);
    ASSERT_TRUE(lost[0].reason.has_value());
    EXPECT_EQ(*lost[0].reason, ErrorCode::NoFeasibleRoute);
    expectConsistent(r);
}

TEST(FailureFixtureB, ItsScheduledEndIsSkippedAndDowntimeStopsThere) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1)});
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 4, 8)}, config(20, {{5, FA::FailLink, 2}}));  // would end at t=9
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(r.metrics.totalReleases, 0u) << "a lost connection is not 'released'";
    EXPECT_DOUBLE_EQ(r.metrics.totalConnectionDowntime, 4.0) << "9 - 5";
    EXPECT_EQ(r.metrics.eventsProcessed, 3u) << "arrival, failure, the (skipped) end";
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u);
    expectConsistent(r);
}

TEST(FailureFixtureB, RecoveryOfTheLinkDoesNotResurrectALostConnection) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1)});
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 4, 100)}, config(20, {{5, FA::FailLink, 2}, {8, FA::RecoverLink, 2}}));
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 0u);
    EXPECT_EQ(r.metrics.linkRecoveries, 1u);
    EXPECT_EQ(r.metrics.failedLinksAtEnd, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.totalConnectionDowntime, 15.0) << "lost at 5, run ends at 20";
    expectConsistent(r);
}

TEST(FailureFixtureB, NoRecoveryPolicyLosesTheConnectionEvenWhenADetourExists) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 4, 100)}, config(10, {{5, FA::FailLink, 2}}, RecoveryPolicy::NoRecovery));
    EXPECT_EQ(r.metrics.connectionsAffected, 1u);
    EXPECT_EQ(r.metrics.connectionsRerouted, 0u);
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u) << "its resources are cleaned";
    const auto lost = entriesOf(r, TraceKind::Lost);
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_FALSE(lost[0].reason.has_value()) << "no reroute was attempted, so there is no failure reason";
    expectConsistent(r);
}

TEST(FailureFixtureB, FreedCapacityOfALostConnectionIsUsableByLaterRequests) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 4), routedLink(2, 2, 3, 1, 1)});
    // #1 fills l1 (4 of 4) and is lost when l2 fails; #2 (A->B, 4 channels) can then use l1.
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 4, 100), arrive(6, 2, 1, 2, 4, 100)}, config(10, {{5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.acceptedRequests, 2u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 4u);
    expectConsistent(r);
}

// ============================================================ Fixture C: link recovery

TEST(FailureFixtureC, FailedLinkIsUnusableUntilItRecovers) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1)});
    const auto r = runScripted(t, {arrive(3, 1, 1, 2, 1, 100), arrive(7, 2, 1, 2, 1, 100)},
                               config(10, {{2, FA::FailLink, 1}, {6, FA::RecoverLink, 1}}));
    EXPECT_EQ(r.metrics.rejectedRequests, 1u);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::NoFeasibleRoute), 1u) << "unavailable, not out of capacity";
    EXPECT_EQ(r.metrics.acceptedRequests, 1u);
    EXPECT_EQ(r.metrics.linkFailures, 1u);
    EXPECT_EQ(r.metrics.linkRecoveries, 1u);
    EXPECT_EQ(r.metrics.failedLinksAtEnd, 0u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 1u);
    ASSERT_GE(r.trace.size(), 4u);
    EXPECT_EQ(r.trace[0].type, EventType::LinkFailure);
    EXPECT_FALSE(r.trace[1].success);  // t=3 arrival rejected
    EXPECT_EQ(r.trace[2].type, EventType::LinkRecovery);
    EXPECT_TRUE(r.trace[3].success);   // t=7 arrival accepted
    expectConsistent(r);
}

TEST(FailureFixtureC, RecoveredLinkIsPreferredAgainByRouting) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(3, 1, 1, 3, 1, 100), arrive(9, 2, 1, 3, 1, 100)},
                               config(12, {{2, FA::FailLink, 2}, {6, FA::RecoverLink, 2}}));
    const auto arrivals = [&] {
        std::vector<TraceEntry> v;
        for (const auto& e : r.trace)
            if (e.kind == TraceKind::Event && e.type == EventType::ConnectionArrival) v.push_back(e);
        return v;
    }();
    ASSERT_EQ(arrivals.size(), 2u);
    EXPECT_EQ(arrivals[0].hops, 3u) << "while l2 is down the detour is used";
    EXPECT_EQ(arrivals[1].hops, 2u) << "after recovery the short route is back";
    expectConsistent(r);
}

// ============================================================ Fixture D: node failure

TEST(FailureFixtureD, TransitNodeFailureReroutesAndBlocksNewRoutesThroughIt) {
    const Topology t = detour();
    const auto r = runScripted(t,
                               {arrive(1, 1, 1, 3, 1, 100),   // via B
                                arrive(6, 2, 1, 2, 1, 100),   // to the failed node itself
                                arrive(7, 3, 1, 3, 1, 100),   // must avoid B
                                arrive(9, 4, 1, 3, 1, 100)},  // after B recovers
                               config(12, {{5, FA::FailNode, 2}, {8, FA::RecoverNode, 2}}));
    const SimulationMetrics& m = r.metrics;
    EXPECT_EQ(m.nodeFailures, 1u);
    EXPECT_EQ(m.nodeRecoveries, 1u);
    EXPECT_EQ(m.linkFailures, 0u);
    EXPECT_EQ(m.connectionsAffected, 1u) << "#1 passes through B";
    EXPECT_EQ(m.connectionsRerouted, 1u);
    EXPECT_EQ(m.rejectedRequests, 1u);
    EXPECT_EQ(m.rejectionsByReason.at(ErrorCode::NoFeasibleRoute), 1u);
    EXPECT_EQ(m.acceptedRequests, 3u);
    EXPECT_EQ(m.failedNodesAtEnd, 0u);
    // #1 rerouted over 3 hops, #3 accepted over 3 hops (B is down), #4 over 2 hops (B is back): 8 channel-links.
    EXPECT_EQ(m.finalAllocatedChannels, 8u);

    std::map<std::uint32_t, TraceEntry> arrival;
    for (const auto& e : r.trace)
        if (e.kind == TraceKind::Event && e.type == EventType::ConnectionArrival) arrival.insert_or_assign(e.connection.value(), e);
    EXPECT_EQ(arrival.at(3).hops, 3u);
    EXPECT_EQ(arrival.at(4).hops, 2u);
    EXPECT_FALSE(arrival.at(2).success);
    const auto rerouted = entriesOf(r, TraceKind::Rerouted);
    ASSERT_EQ(rerouted.size(), 1u);
    EXPECT_EQ(rerouted[0].hops, 3u);
    EXPECT_EQ(rerouted[0].type, EventType::NodeFailure);
    EXPECT_EQ(rerouted[0].resource, 2u);
    expectConsistent(r);
}

TEST(FailureFixtureD, ConnectionWhoseEndpointFailsIsLost) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 2, 100)}, config(10, {{5, FA::FailNode, 3}}));  // node C is the destination
    EXPECT_EQ(r.metrics.connectionsAffected, 1u);
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u);
    expectConsistent(r);
}

TEST(FailureFixtureD, NodeFailureLeavesTheTopologyIntact) {
    const Topology t = detour();
    (void)runScripted(t, {arrive(1, 1, 1, 3, 1, 100)}, config(10, {{5, FA::FailNode, 2}}));
    EXPECT_EQ(t.nodeCount(), 5u);
    EXPECT_EQ(t.linkCount(), 5u);
    EXPECT_TRUE(t.hasNode(NodeId{2}));
    EXPECT_EQ(t.degree(NodeId{2}), 2u);
}

// ============================================================ Fixture E: capacity-constrained reroute

TEST(FailureFixtureE, FullDetourMeansTheRerouteFailsCleanly) {
    const Topology t = detour();
    // #2 fills the detour's first link A-D (l3) completely; #1 then cannot move there.
    const auto r = runScripted(t, {arrive(1, 2, 1, 4, 10, 100), arrive(2, 1, 1, 3, 4, 100)}, config(10, {{5, FA::FailLink, 2}}));
    const SimulationMetrics& m = r.metrics;
    EXPECT_EQ(m.connectionsAffected, 1u);
    EXPECT_EQ(m.connectionsLost, 1u);
    const auto lost = entriesOf(r, TraceKind::Lost);
    ASSERT_EQ(lost.size(), 1u);
    ASSERT_TRUE(lost[0].reason.has_value());
    EXPECT_EQ(*lost[0].reason, ErrorCode::InsufficientCapacity) << "a detour exists but has no free channel";
    EXPECT_EQ(m.finalAllocatedChannels, 10u) << "only the unrelated connection #2 remains; no partial allocation of #1";
    EXPECT_EQ(m.activeConnectionsAtEnd, 1u);
    expectConsistent(r);
}

// ============================================================ constraints and demand preservation

TEST(FailureReroute, OriginalHopLimitIsNotRelaxedDuringRecovery) {
    const Topology t = detour();
    RoutingConstraints limited;
    limited.maxHops = 2;
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 1, 100, limited), arrive(1, 2, 1, 3, 1, 100)}, config(10, {{5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsAffected, 2u);
    EXPECT_EQ(r.metrics.connectionsRerouted, 1u);
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(entriesOf(r, TraceKind::Lost)[0].connection, ConnectionId{1}) << "the hop-limited one";
    EXPECT_EQ(entriesOf(r, TraceKind::Rerouted)[0].connection, ConnectionId{2});
    expectConsistent(r);
}

TEST(FailureReroute, OriginalBlockedNodesAreRespected) {
    const Topology t = detour();
    RoutingConstraints c;
    c.blockedNodes.insert(NodeId{5});  // the detour passes through E
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 1, 100, c)}, config(10, {{5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(r.metrics.connectionsRerouted, 0u);
}

TEST(FailureReroute, MaxCostIsPreserved) {
    const Topology t = detour();
    RoutingConstraints c;
    c.maxCost = 2.5;  // A-B-C costs 2, the detour 3
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 1, 100, c)}, config(10, {{5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
}

TEST(FailureReroute, DemandIsPreservedExactly) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 7, 100)}, config(10, {{5, FA::FailLink, 1}}));
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 21u) << "7 channels over the 3-hop detour";
    EXPECT_EQ(entriesOf(r, TraceKind::Rerouted)[0].capacityChannels, 7u);
}

TEST(FailureReroute, AffectedConnectionsAreRecoveredInAscendingIdOrderAndShareFreedCapacity) {
    // l1 and l2 hold 12 channels, the detour links 10. Two connections of 6 cannot both move onto the detour:
    // the lower id wins it.
    const auto bi = LinkDirection::Bidirectional;
    const Topology t = makeTopology({1, 2, 3, 4, 5}, {routedLink(1, 1, 2, 1, 1, bi, 12), routedLink(2, 2, 3, 1, 1, bi, 12),
                                                       routedLink(3, 1, 4, 1, 1, bi, 10), routedLink(4, 4, 5, 1, 1, bi, 10),
                                                       routedLink(5, 5, 3, 1, 1, bi, 10)});
    const auto r = runScripted(t, {arrive(1, 7, 1, 3, 6, 100), arrive(2, 3, 1, 3, 6, 100)}, config(10, {{5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsAffected, 2u);
    EXPECT_EQ(r.metrics.connectionsRerouted, 1u);
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_DOUBLE_EQ(r.metrics.recoverySuccessRate, 0.5);
    EXPECT_EQ(entriesOf(r, TraceKind::Rerouted)[0].connection, ConnectionId{3}) << "id 3 < id 7";
    EXPECT_EQ(entriesOf(r, TraceKind::Lost)[0].connection, ConnectionId{7});
    // Trace order of the consequences: Affected 3, Affected 7, then Rerouted 3, Lost 7.
    std::vector<std::pair<TraceKind, std::uint32_t>> order;
    for (const auto& e : r.trace)
        if (e.kind != TraceKind::Event) order.emplace_back(e.kind, e.connection.value());
    EXPECT_EQ(order, (std::vector<std::pair<TraceKind, std::uint32_t>>{{TraceKind::Affected, 3}, {TraceKind::Affected, 7},
                                                                      {TraceKind::Rerouted, 3}, {TraceKind::Lost, 7}}));
    expectConsistent(r);
}

TEST(FailureReroute, TwoSuccessiveFailuresCanRerouteTheSameConnectionTwice) {
    const auto bi = LinkDirection::Bidirectional;
    // Three disjoint 2-hop routes A-B-C, A-D-C, A-E-C with increasing cost.
    const Topology t = makeTopology({1, 2, 3, 4, 5}, {routedLink(1, 1, 2, 1, 1, bi, 10), routedLink(2, 2, 3, 1, 1, bi, 10),
                                                       routedLink(3, 1, 4, 1, 2, bi, 10), routedLink(4, 4, 3, 1, 2, bi, 10),
                                                       routedLink(5, 1, 5, 1, 3, bi, 10), routedLink(6, 5, 3, 1, 3, bi, 10)});
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 2, 100)}, config(10, {{3, FA::FailLink, 2}, {6, FA::FailLink, 4}}));
    EXPECT_EQ(r.metrics.connectionsAffected, 2u);
    EXPECT_EQ(r.metrics.connectionsRerouted, 2u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 4u);
    EXPECT_EQ(r.metrics.failedLinksAtEnd, 2u);
    expectConsistent(r);
}

TEST(FailureReroute, ALostConnectionsIdStaysReservedUntilItsScheduledEnd) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1)});
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 1, 50), arrive(6, 1, 1, 2, 1, 5)}, config(20, {{5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::DuplicateId), 1u) << "id 1 is still held by the lost connection";
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 0u);
    expectConsistent(r);
}

// ============================================================ event ordering with failures

TEST(FailureOrdering, FailureAndArrivalAtTheSameTimeFailureFirst) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(5, 1, 1, 3, 1, 100)}, config(10, {{5, FA::FailLink, 2}}));
    ASSERT_GE(r.trace.size(), 2u);
    EXPECT_EQ(r.trace[0].type, EventType::LinkFailure);
    EXPECT_EQ(r.trace[1].type, EventType::ConnectionArrival);
    EXPECT_EQ(r.trace[1].hops, 3u) << "the arrival already sees the failure";
}

TEST(FailureOrdering, RecoveryAndArrivalAtTheSameTimeRecoveryFirst) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(6, 1, 1, 3, 1, 100)}, config(10, {{2, FA::FailLink, 2}, {6, FA::RecoverLink, 2}}));
    EXPECT_EQ(r.trace[1].type, EventType::LinkRecovery);
    EXPECT_EQ(r.trace[2].type, EventType::ConnectionArrival);
    EXPECT_EQ(r.trace[2].hops, 2u) << "the arrival already sees the recovery";
}

TEST(FailureOrdering, FailureAndReleaseAtTheSameTimeFailureFirst) {
    const Topology t = detour();
    // #1 ends at t=5, exactly when l2 fails: the failure is processed first, so #1 is affected and rerouted, then released.
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 2, 4)}, config(10, {{5, FA::FailLink, 2}}));
    std::vector<std::pair<TraceKind, EventType>> at5;
    for (const auto& e : r.trace)
        if (e.time == 5.0) at5.emplace_back(e.kind, e.type);
    EXPECT_EQ(at5, (std::vector<std::pair<TraceKind, EventType>>{{TraceKind::Event, EventType::LinkFailure},
                                                                {TraceKind::Affected, EventType::LinkFailure},
                                                                {TraceKind::Rerouted, EventType::LinkFailure},
                                                                {TraceKind::Event, EventType::ConnectionRelease}}));
    EXPECT_EQ(r.metrics.connectionsAffected, 1u);
    EXPECT_EQ(r.metrics.totalReleases, 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u);
    expectConsistent(r);
}

TEST(FailureOrdering, RecoveryBeforeFailureAtTheSameTimeLetsTheRerouteUseWhatRecovered) {
    const Topology t = detour();
    // l4 (D-E) is down from t=2. At t=6 it recovers while l2 fails: recovery first, so the detour is usable.
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 1, 100)},
                               config(10, {{0.5, FA::FailLink, 4}, {6, FA::RecoverLink, 4}, {6, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsRerouted, 1u);
    EXPECT_EQ(r.metrics.connectionsLost, 0u);
    expectConsistent(r);
}

TEST(FailureOrdering, RecoverThenFailTheSameLinkAtOneInstantEndsFailed) {
    const Topology t = detour();
    const auto r = runScripted(t, {}, config(10, {{2, FA::FailLink, 2}, {5, FA::RecoverLink, 2}, {5, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.linkFailures, 2u);
    EXPECT_EQ(r.metrics.linkRecoveries, 1u);
    EXPECT_EQ(r.metrics.failedLinksAtEnd, 1u);
}

TEST(FailureOrdering, FailureAndRecoveryOfDifferentResourcesAtTheSameTimeRecoveryFirst) {
    const Topology t = detour();
    const auto r = runScripted(t, {}, config(10, {{1, FA::FailLink, 1}, {4, FA::FailLink, 2}, {4, FA::RecoverLink, 1}}));
    ASSERT_EQ(r.trace.size(), 3u);
    EXPECT_EQ(r.trace[1].type, EventType::LinkRecovery);
    EXPECT_EQ(r.trace[2].type, EventType::LinkFailure);
    EXPECT_EQ(r.metrics.failedLinksAtEnd, 1u);
}

TEST(FailureOrdering, FailuresBeyondTheHorizonAreNotProcessed) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 1, 100)}, config(10, {{50, FA::FailLink, 2}}));
    EXPECT_EQ(r.metrics.totalFailureEvents, 0u);
    EXPECT_EQ(r.metrics.connectionsAffected, 0u);
}

// ============================================================ validation of failure input

TEST(FailureInput, UnknownIdsAndInvalidTransitionsAreRejectedBeforeRunning) {
    const Topology t = detour();
    ASSERT_ERROR(SimulationEngine{}.runScript(t, {}, config(10, {{1, FA::FailLink, 99}})), ErrorCode::NotFound);
    ASSERT_ERROR(SimulationEngine{}.runScript(t, {}, config(10, {{1, FA::FailNode, 99}})), ErrorCode::NotFound);
    ASSERT_ERROR(SimulationEngine{}.runScript(t, {}, config(10, {{1, FA::RecoverLink, 1}})), ErrorCode::InvalidTransition);
    ASSERT_ERROR(SimulationEngine{}.runScript(t, {}, config(10, {{1, FA::FailLink, 1}, {2, FA::FailLink, 1}})), ErrorCode::InvalidTransition);
    ASSERT_ERROR(SimulationEngine{}.runScript(t, {}, config(10, {{-1, FA::FailLink, 1}})), ErrorCode::InvalidArgument);
}

TEST(FailureInput, GeneratedRunsValidateTheScheduleToo) {
    const Topology t = detour();
    SimulationConfig c;
    c.failureSchedule = {{1, FA::FailLink, 77}};
    ASSERT_ERROR(SimulationEngine{}.run(t, c), ErrorCode::NotFound);
    c.failureSchedule = {{1, FA::RecoverLink, 1}};
    ASSERT_ERROR(SimulationEngine{}.run(t, c), ErrorCode::InvalidTransition);
}

TEST(FailureInput, ScriptedAndRandomFailuresCannotBeCombined) {
    SimulationConfig c;
    c.failureSchedule = {{1, FA::FailLink, 1}};
    c.randomFailures = RandomFailureConfig{};
    ASSERT_ERROR(SimulationEngine{}.run(detour(), c), ErrorCode::InvalidArgument);
    c.failureSchedule.clear();
    c.randomFailures->meanRepairTime = 0;
    ASSERT_ERROR(SimulationEngine{}.run(detour(), c), ErrorCode::InvalidArgument);
}

TEST(FailureInput, NoFailuresMeansNoFailureMetrics) {
    const Topology t = detour();
    const auto r = runScripted(t, {arrive(1, 1, 1, 3, 1, 100)}, config(10, {}));
    const SimulationMetrics& m = r.metrics;
    EXPECT_EQ(m.totalFailureEvents + m.linkRecoveries + m.nodeRecoveries + m.connectionsAffected + m.connectionsLost, 0u);
    EXPECT_DOUBLE_EQ(m.totalConnectionDowntime, 0.0);
}

// ============================================================ random failures inside a run

namespace {

Topology grid(std::uint32_t side, std::uint32_t channels) {
    const auto node = [side](std::uint32_t r, std::uint32_t c) { return r * side + c + 1; };
    Topology t;
    for (std::uint32_t i = 1; i <= side * side; ++i) EXPECT_TRUE(t.addNode(makeNode(i)).ok());
    std::uint32_t link = 1;
    for (std::uint32_t r = 0; r < side; ++r)
        for (std::uint32_t c = 0; c < side; ++c) {
            if (c + 1 < side) EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r, c + 1), 10, 1, LinkDirection::Bidirectional, channels)).ok());
            if (r + 1 < side) EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r + 1, c), 10, 1, LinkDirection::Bidirectional, channels)).ok());
        }
    return t;
}

SimulationConfig randomFailureConfig(std::uint64_t seed) {
    SimulationConfig c;
    c.seed = seed;
    c.endTime = 200.0;
    c.arrivalRate = 3.0;
    c.meanLifetime = 15.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    RandomFailureConfig f;
    f.failureRate = 0.15;
    f.meanRepairTime = 12.0;
    f.nodeFailureProbability = 0.25;
    c.randomFailures = f;
    return c;
}

}  // namespace

TEST(RandomFailureRun, SameSeedGivesIdenticalResults) {
    const Topology t = grid(5, 6);
    const auto a = must(SimulationEngine{}.run(t, randomFailureConfig(11)));
    const auto b = must(SimulationEngine{}.run(t, randomFailureConfig(11)));
    EXPECT_EQ(a, b);
    EXPECT_GT(a.metrics.totalFailureEvents, 5u);
    EXPECT_GT(a.metrics.connectionsAffected, 0u);
}

TEST(RandomFailureRun, DifferentSeedsChangeTheFailuresAndTheWorkload) {
    const Topology t = grid(5, 6);
    EXPECT_NE(must(SimulationEngine{}.run(t, randomFailureConfig(1))).trace, must(SimulationEngine{}.run(t, randomFailureConfig(2))).trace);
}

TEST(RandomFailureRun, EnablingFailuresDoesNotChangeTheRequestWorkload) {
    const Topology t = grid(5, 6);
    auto with = randomFailureConfig(21);
    auto without = with;
    without.randomFailures.reset();
    const auto arrivals = [](const SimulationResult& r) {
        std::vector<std::tuple<double, ConnectionId, NodeId, NodeId, std::uint32_t>> v;
        for (const auto& e : r.trace)
            if (e.kind == TraceKind::Event && e.type == EventType::ConnectionArrival) v.emplace_back(e.time, e.connection, e.source, e.destination, e.capacityChannels);
        return v;
    };
    const auto a = must(SimulationEngine{}.run(t, with));
    const auto b = must(SimulationEngine{}.run(t, without));
    EXPECT_EQ(arrivals(a), arrivals(b)) << "failures use their own RNG stream";
}

TEST(RandomFailureRun, ResourcesStayConsistentAndMetricsAddUp) {
    const Topology t = grid(5, 4);
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        const auto r = must(SimulationEngine{}.run(t, randomFailureConfig(seed)));
        expectConsistent(r);
        EXPECT_GT(r.metrics.totalFailureEvents, 0u) << seed;
    }
}

TEST(RandomFailureRun, NoRecoveryLosesEverythingTheFailuresHit) {
    const Topology t = grid(5, 6);
    auto c = randomFailureConfig(5);
    c.recoveryPolicy = RecoveryPolicy::NoRecovery;
    const auto none = must(SimulationEngine{}.run(t, c));
    c.recoveryPolicy = RecoveryPolicy::Reroute;
    const auto reroute = must(SimulationEngine{}.run(t, c));
    EXPECT_EQ(none.metrics.connectionsRerouted, 0u);
    EXPECT_EQ(none.metrics.connectionsLost, none.metrics.connectionsAffected);
    EXPECT_GT(reroute.metrics.connectionsRerouted, 0u) << "a grid usually offers alternatives";
    EXPECT_GE(reroute.metrics.recoverySuccessRate, none.metrics.recoverySuccessRate);
    expectConsistent(none);
    expectConsistent(reroute);
}

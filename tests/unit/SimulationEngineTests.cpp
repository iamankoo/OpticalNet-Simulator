#include <gtest/gtest.h>

#include <chrono>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/simulation/SimulationEngine.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// A(1) --l1(10 ch)-- B(2) --l2(10 ch)-- C(3), every link has admin cost 1.
Topology lineABC() {
    return makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 10),
                                    routedLink(2, 2, 3, 1, 1, LinkDirection::Bidirectional, 10)});
}

ScriptedArrival arrive(double time, std::uint32_t id, std::uint32_t from, std::uint32_t to, std::uint32_t demand, double lifetime,
                       RoutingConstraints constraints = {}) {
    return ScriptedArrival{time, ConnectionRequest{ConnectionId{id}, NodeId{from}, NodeId{to}, demand, std::move(constraints)}, lifetime};
}

SimulationConfig scriptConfig(double endTime, bool trace = true) {
    SimulationConfig c;
    c.endTime = endTime;
    c.recordTrace = trace;
    return c;
}

SimulationResult runScript(const Topology& t, const std::vector<ScriptedArrival>& script, const SimulationConfig& c) {
    return must(SimulationEngine{}.runScript(t, script, c));
}

SimulationConfig gridConfig(std::uint64_t seed) {
    SimulationConfig c;
    c.seed = seed;
    c.endTime = 300.0;
    c.arrivalRate = 4.0;
    c.meanLifetime = 10.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    return c;
}

Topology grid(std::uint32_t side, std::uint32_t channels) {
    const auto node = [side](std::uint32_t r, std::uint32_t c) { return r * side + c + 1; };
    Topology t;
    for (std::uint32_t i = 1; i <= side * side; ++i) EXPECT_TRUE(t.addNode(makeNode(i)).ok());
    std::uint32_t link = 1;
    for (std::uint32_t r = 0; r < side; ++r) {
        for (std::uint32_t c = 0; c < side; ++c) {
            if (c + 1 < side) EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r, c + 1), 10, 1, LinkDirection::Bidirectional, channels)).ok());
            if (r + 1 < side) EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r + 1, c), 10, 1, LinkDirection::Bidirectional, channels)).ok());
        }
    }
    return t;
}

}  // namespace

// ================================================================== hand-computed fixture

// Expected values worked out by hand (see the timeline in the comments).
TEST(SimulationFixture, ScriptedLineMatchesTheHandComputedResult) {
    const Topology t = lineABC();
    const auto r = runScript(t,
                             {
                                 arrive(1, 1, 1, 3, 6, 5),   // A->C, 6 ch over l1,l2: accepted, release at 6
                                 arrive(2, 2, 1, 2, 5, 10),  // A->B, 5 ch: l1 has only 4 free: rejected (capacity)
                                 arrive(3, 3, 3, 2, 4, 1),   // C->B, 4 ch: l2 has exactly 4 free: accepted, release at 4
                                 arrive(4, 4, 2, 3, 3, 100), // B->C, 3 ch at t=4: the release of #3 happens first, so it fits
                             },
                             scriptConfig(10));
    const SimulationMetrics& m = r.metrics;

    EXPECT_EQ(m.totalRequests, 4u);
    EXPECT_EQ(m.acceptedRequests, 3u);
    EXPECT_EQ(m.rejectedRequests, 1u);
    EXPECT_EQ(m.rejectionsByReason.at(ErrorCode::InsufficientCapacity), 1u);
    EXPECT_EQ(m.rejectionsByReason.size(), 1u);
    EXPECT_DOUBLE_EQ(m.acceptanceRate, 0.75);
    EXPECT_DOUBLE_EQ(m.blockingRate, 0.25);

    EXPECT_EQ(m.totalCapacityChannels, 20u);
    EXPECT_EQ(m.peakAllocatedChannels, 16u) << "12 (#1) + 4 (#3) at t=3";
    // Allocated channel-links over time: 0 on [0,1), 12 on [1,3), 16 on [3,4), 15 on [4,6), 3 on [6,10].
    // Integral = 0 + 24 + 16 + 30 + 12 = 82;  82 / (20 * 10) = 0.41
    EXPECT_DOUBLE_EQ(m.averageUtilization, 0.41);
    EXPECT_EQ(m.finalAllocatedChannels, 3u);
    EXPECT_EQ(m.finalAvailableChannels, 17u);

    EXPECT_DOUBLE_EQ(m.averageHopCount, 4.0 / 3.0) << "(2 + 1 + 1) / 3";
    EXPECT_DOUBLE_EQ(m.averagePathCost, 4.0 / 3.0);
    EXPECT_EQ(m.maxHopCount, 2u);

    EXPECT_EQ(m.activeConnectionsAtEnd, 1u);
    EXPECT_EQ(m.peakActiveConnections, 2u);
    EXPECT_EQ(m.totalReleases, 2u) << "#3 at t=4 and #1 at t=6; #4's release (t=104) is beyond the horizon";
    EXPECT_EQ(m.eventsProcessed, 6u);
    EXPECT_DOUBLE_EQ(m.duration, 10.0);
}

TEST(SimulationFixture, ScriptedLineTraceShowsTheExactEventOrder) {
    const Topology t = lineABC();
    const auto r = runScript(t,
                             {arrive(1, 1, 1, 3, 6, 5), arrive(2, 2, 1, 2, 5, 10), arrive(3, 3, 3, 2, 4, 1), arrive(4, 4, 2, 3, 3, 100)},
                             scriptConfig(10));
    ASSERT_EQ(r.trace.size(), 6u);
    const auto expect = [&](std::size_t i, double time, EventType type, std::uint32_t id, bool success) {
        EXPECT_DOUBLE_EQ(r.trace[i].time, time) << i;
        EXPECT_EQ(r.trace[i].type, type) << i;
        EXPECT_EQ(r.trace[i].connection, ConnectionId{id}) << i;
        EXPECT_EQ(r.trace[i].success, success) << i;
    };
    expect(0, 1, EventType::ConnectionArrival, 1, true);
    expect(1, 2, EventType::ConnectionArrival, 2, false);
    expect(2, 3, EventType::ConnectionArrival, 3, true);
    expect(3, 4, EventType::ConnectionRelease, 3, true);  // same time as #4's arrival, but first
    expect(4, 4, EventType::ConnectionArrival, 4, true);
    expect(5, 6, EventType::ConnectionRelease, 1, true);
    EXPECT_EQ(r.trace[0].hops, 2u);
    EXPECT_EQ(r.trace[1].hops, 0u);
    ASSERT_TRUE(r.trace[1].reason.has_value());
    EXPECT_EQ(*r.trace[1].reason, ErrorCode::InsufficientCapacity);
    EXPECT_FALSE(r.trace[0].reason.has_value());
}

// The same kind of hand computation, but through the request generator: with fixed timings the
// workload is fully predictable (2 nodes, one 10-channel link, 4 channels per request).
TEST(SimulationFixture, GeneratedFixedWorkloadMatchesTheHandComputedResult) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 10)});
    SimulationConfig c;
    c.endTime = 10.0;
    c.arrivalRate = 1.0;  // arrivals at t = 1, 2, ..., 10
    c.arrivalTiming = Timing::Fixed;
    c.meanLifetime = 2.5;
    c.lifetimeTiming = Timing::Fixed;
    c.minCapacityChannels = c.maxCapacityChannels = 4;  // at most two requests fit at a time
    c.recordTrace = true;
    const auto r = must(SimulationEngine{}.run(t, c));
    const SimulationMetrics& m = r.metrics;

    // Accepted: #1,#2,#4,#5,#7,#8,#10   Rejected: #3,#6,#9 (the link already holds 8 of 10 channels).
    EXPECT_EQ(m.totalRequests, 10u);
    EXPECT_EQ(m.acceptedRequests, 7u);
    EXPECT_EQ(m.rejectedRequests, 3u);
    EXPECT_DOUBLE_EQ(m.acceptanceRate, 0.7);
    EXPECT_DOUBLE_EQ(m.blockingRate, 0.3);
    EXPECT_EQ(m.totalReleases, 5u) << "releases at 3.5, 4.5, 6.5, 7.5, 9.5";
    EXPECT_EQ(m.activeConnectionsAtEnd, 2u);
    EXPECT_EQ(m.peakAllocatedChannels, 8u);
    EXPECT_EQ(m.peakActiveConnections, 2u);
    EXPECT_EQ(m.finalAllocatedChannels, 8u);
    EXPECT_EQ(m.finalAvailableChannels, 2u);
    // Allocated on [1,2): 4; [2,3.5): 8; [3.5,4): 4; [4,4.5): 8; [4.5,5): 4; [5,6.5): 8; [6.5,7): 4; [7,7.5): 8;
    // [7.5,8): 4; [8,9.5): 8; [9.5,10): 4  ->  integral 58;  58 / (10 * 10) = 0.58
    EXPECT_DOUBLE_EQ(m.averageUtilization, 0.58);
    EXPECT_DOUBLE_EQ(m.averageHopCount, 1.0);
    EXPECT_EQ(m.maxHopCount, 1u);
    EXPECT_EQ(m.eventsProcessed, 15u);

    std::vector<std::uint32_t> rejected;
    for (const auto& e : r.trace) {
        if (e.type == EventType::ConnectionArrival && !e.success) rejected.push_back(e.connection.value());
    }
    EXPECT_EQ(rejected, (std::vector<std::uint32_t>{3, 6, 9}));
}

// ================================================================== scripted scenarios

TEST(SimulationScript, NoArrivalsGivesAnIdleNetwork) {
    const auto r = runScript(lineABC(), {}, scriptConfig(50));
    const auto& m = r.metrics;
    EXPECT_EQ(m.totalRequests, 0u);
    EXPECT_DOUBLE_EQ(m.acceptanceRate, 0.0);
    EXPECT_DOUBLE_EQ(m.blockingRate, 0.0);
    EXPECT_DOUBLE_EQ(m.averageUtilization, 0.0);
    EXPECT_DOUBLE_EQ(m.averagePathCost, 0.0);
    EXPECT_EQ(m.totalCapacityChannels, 20u);
    EXPECT_EQ(m.finalAvailableChannels, 20u);
    EXPECT_DOUBLE_EQ(m.duration, 50.0);
    EXPECT_EQ(m.eventsProcessed, 0u);
}

TEST(SimulationScript, OneRequestAccepted) {
    const auto r = runScript(lineABC(), {arrive(2, 1, 1, 3, 5, 100)}, scriptConfig(10));
    const auto& m = r.metrics;
    EXPECT_EQ(m.acceptedRequests, 1u);
    EXPECT_EQ(m.rejectedRequests, 0u);
    EXPECT_DOUBLE_EQ(m.acceptanceRate, 1.0);
    EXPECT_DOUBLE_EQ(m.blockingRate, 0.0);
    EXPECT_EQ(m.finalAllocatedChannels, 10u);
    EXPECT_EQ(m.activeConnectionsAtEnd, 1u);
    EXPECT_TRUE(m.rejectionsByReason.empty());
    // 10 channel-links held from t=2 to t=10: 80 / (20 * 10) = 0.4
    EXPECT_DOUBLE_EQ(m.averageUtilization, 0.4);
}

TEST(SimulationScript, OneRequestRejectedForCapacity) {
    const auto r = runScript(lineABC(), {arrive(1, 1, 1, 2, 11, 5)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.acceptedRequests, 0u);
    EXPECT_EQ(r.metrics.rejectedRequests, 1u);
    EXPECT_DOUBLE_EQ(r.metrics.blockingRate, 1.0);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::InsufficientCapacity), 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.0);
    EXPECT_DOUBLE_EQ(r.metrics.averageHopCount, 0.0) << "no accepted connections: averages are 0, not NaN";
}

TEST(SimulationScript, ConnectionIsReleasedAtArrivalPlusLifetime) {
    const auto r = runScript(lineABC(), {arrive(2, 1, 1, 2, 4, 3)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.totalReleases, 1u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 0u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u);
    ASSERT_EQ(r.trace.size(), 2u);
    EXPECT_DOUBLE_EQ(r.trace[1].time, 5.0);
    EXPECT_EQ(r.trace[1].type, EventType::ConnectionRelease);
    // 4 channels from t=2 to t=5: 12 / (20 * 10) = 0.06
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.06);
}

TEST(SimulationScript, NoFeasibleRouteIsReportedAsSuch) {
    RoutingConstraints c;
    c.maxCost = 1.0;  // A->C costs 2
    const auto r = runScript(lineABC(), {arrive(1, 1, 1, 3, 1, 5, c)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::NoFeasibleRoute), 1u);
}

TEST(SimulationScript, UnreachableDestinationIsNoRoute) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1)});
    const auto r = runScript(t, {arrive(1, 1, 1, 3, 1, 5)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::NoRoute), 1u);
}

TEST(SimulationScript, UnknownNodeAndDuplicateIdAreRejectedNotFatal) {
    const auto r = runScript(lineABC(), {arrive(1, 1, 1, 2, 1, 100), arrive(2, 1, 2, 3, 1, 5), arrive(3, 2, 1, 99, 1, 5)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.acceptedRequests, 1u);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::DuplicateId), 1u);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::NotFound), 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 1u) << "the rejected duplicate must not have allocated or released anything";
}

TEST(SimulationScript, ResourcesAreReusedAfterRelease) {
    // The link holds 10 channels. #1 fills it until t=3; #2 at t=2 is blocked; #3 at exactly t=3 fits because
    // the release at t=3 is processed before the arrival at t=3.
    const auto r = runScript(lineABC(), {arrive(1, 1, 1, 2, 10, 2), arrive(2, 2, 1, 2, 10, 2), arrive(3, 3, 1, 2, 10, 2)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.acceptedRequests, 2u);
    EXPECT_EQ(r.metrics.rejectedRequests, 1u);
    ASSERT_GE(r.trace.size(), 4u);
    EXPECT_EQ(r.trace[2].type, EventType::ConnectionRelease);
    EXPECT_EQ(r.trace[3].type, EventType::ConnectionArrival);
    EXPECT_TRUE(r.trace[3].success);
}

TEST(SimulationScript, ArrivalsAtTheSameTimeAreProcessedInScriptOrder) {
    // Both want the whole link at t=1: the first in the script wins.
    const auto r = runScript(lineABC(), {arrive(1, 7, 1, 2, 10, 5), arrive(1, 3, 1, 2, 10, 5)}, scriptConfig(10));
    ASSERT_EQ(r.trace.size(), 3u) << "two arrivals plus the release of #7 at t=6";
    EXPECT_EQ(r.trace[0].connection, ConnectionId{7});
    EXPECT_TRUE(r.trace[0].success);
    EXPECT_FALSE(r.trace[1].success);
}

TEST(SimulationScript, ArrivalsAfterTheHorizonAreIgnoredAndLateReleasesStayPending) {
    const auto r = runScript(lineABC(), {arrive(5, 1, 1, 2, 1, 100), arrive(11, 2, 1, 2, 1, 1)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.totalRequests, 1u);
    EXPECT_EQ(r.metrics.totalReleases, 0u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 1u);
}

TEST(SimulationScript, ArrivalExactlyAtTheHorizonIsProcessed) {
    const auto r = runScript(lineABC(), {arrive(10, 1, 1, 2, 1, 5)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.totalRequests, 1u);
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.0) << "it holds capacity for zero time within the window";
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 1u);
}

TEST(SimulationScript, VeryShortLifetimesWork) {
    const auto r = runScript(lineABC(), {arrive(1, 1, 1, 2, 5, 1e-9), arrive(1, 2, 1, 2, 5, 1e-9)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.acceptedRequests, 2u);
    EXPECT_EQ(r.metrics.totalReleases, 2u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u);
}

TEST(SimulationScript, ExtremelyLargeCapacityLinksDoNotOverflow) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 4'000'000'000u),
                                             routedLink(2, 1, 2, 1, 2, LinkDirection::Bidirectional, 4'000'000'000u)});
    const auto r = runScript(t, {arrive(1, 1, 1, 2, 4'000'000'000u, 5), arrive(2, 2, 1, 2, 4'000'000'000u, 5), arrive(3, 3, 1, 2, 1, 5)}, scriptConfig(10));
    EXPECT_EQ(r.metrics.totalCapacityChannels, 8'000'000'000ULL);
    EXPECT_EQ(r.metrics.acceptedRequests, 2u) << "each parallel link carries one full-size request";
    EXPECT_EQ(r.metrics.rejectedRequests, 1u);
    EXPECT_EQ(r.metrics.peakAllocatedChannels, 8'000'000'000ULL);
    EXPECT_GT(r.metrics.averageUtilization, 0.0);
    EXPECT_LE(r.metrics.averageUtilization, 1.0);
}

TEST(SimulationScript, AbsurdlyLongLifetimeDoesNotOverflowTheClock) {
    // 1.5e308 + 1.7e308 would be infinity; the release is clamped and simply never happens within the horizon.
    const auto r = runScript(lineABC(), {arrive(1.5e308, 1, 1, 2, 1, 1.7e308)}, scriptConfig(1.7e308));
    EXPECT_EQ(r.metrics.acceptedRequests, 1u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 1u);
    EXPECT_EQ(r.metrics.totalReleases, 0u);
}

TEST(SimulationScript, InvalidScriptsAreRejectedBeforeRunning) {
    const Topology t = lineABC();
    ASSERT_ERROR(SimulationEngine{}.runScript(t, std::vector{arrive(-1, 1, 1, 2, 1, 5)}, scriptConfig(10)), ErrorCode::InvalidArgument);
    ASSERT_ERROR(SimulationEngine{}.runScript(t, std::vector{arrive(1, 1, 1, 2, 1, 0)}, scriptConfig(10)), ErrorCode::InvalidArgument);
    ASSERT_ERROR(SimulationEngine{}.runScript(t, std::vector{arrive(1, 1, 1, 2, 1, -2)}, scriptConfig(10)), ErrorCode::InvalidArgument);
    ASSERT_ERROR(SimulationEngine{}.runScript(t, {}, scriptConfig(-5)), ErrorCode::InvalidArgument);
}

TEST(SimulationScript, ZeroDurationRunProcessesNothingAfterTimeZero) {
    const auto r = runScript(lineABC(), {arrive(1, 1, 1, 2, 1, 5)}, scriptConfig(0));
    EXPECT_EQ(r.metrics.totalRequests, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.duration, 0.0);
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.0) << "no division by zero";
}

TEST(SimulationScript, TraceIsOnlyRecordedWhenRequested) {
    EXPECT_TRUE(runScript(lineABC(), {arrive(1, 1, 1, 2, 1, 5)}, scriptConfig(10, false)).trace.empty());
    EXPECT_FALSE(runScript(lineABC(), {arrive(1, 1, 1, 2, 1, 5)}, scriptConfig(10, true)).trace.empty());
}

TEST(SimulationScript, SimulationDoesNotModifyTheTopologyAndCanBeRepeated) {
    const Topology t = lineABC();
    const std::vector<ScriptedArrival> script{arrive(1, 1, 1, 3, 6, 5), arrive(2, 2, 1, 3, 6, 5)};
    const auto first = runScript(t, script, scriptConfig(10));
    const auto second = runScript(t, script, scriptConfig(10));
    EXPECT_EQ(first, second) << "each run starts from a clean network";
    EXPECT_EQ(t.linkCount(), 2u);
}

// ================================================================== generated runs: edge cases

TEST(SimulationRun, EmptyTopologyGivesAnEmptyResult) {
    SimulationConfig c;
    c.endTime = 100;
    const auto r = must(SimulationEngine{}.run(Topology{}, c));
    EXPECT_EQ(r.metrics.totalRequests, 0u);
    EXPECT_EQ(r.metrics.totalCapacityChannels, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.0);
    EXPECT_DOUBLE_EQ(r.metrics.duration, 100.0);
}

TEST(SimulationRun, SingleNodeTopologyHasNoPossiblePair) {
    SimulationConfig c;
    const auto r = must(SimulationEngine{}.run(makeTopology({1}, {}), c));
    EXPECT_EQ(r.metrics.totalRequests, 0u);
}

TEST(SimulationRun, ZeroRequestRateGeneratesNothingAndRunsToTheHorizon) {
    SimulationConfig c;
    c.arrivalRate = 0.0;
    c.endTime = 80.0;
    const auto r = must(SimulationEngine{}.run(lineABC(), c));
    EXPECT_EQ(r.metrics.totalRequests, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.duration, 80.0);
    EXPECT_EQ(r.metrics.finalAvailableChannels, r.metrics.totalCapacityChannels);
}

TEST(SimulationRun, ZeroDurationRun) {
    SimulationConfig c;
    c.endTime = 0.0;
    const auto r = must(SimulationEngine{}.run(lineABC(), c));
    EXPECT_EQ(r.metrics.totalRequests, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.duration, 0.0);
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.0);
}

TEST(SimulationRun, ZeroRequestLimit) {
    SimulationConfig c;
    c.maxRequests = 0;
    const auto r = must(SimulationEngine{}.run(lineABC(), c));
    EXPECT_EQ(r.metrics.totalRequests, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.duration, 0.0) << "stopped by the cap with nothing scheduled: ends at the last event (none)";
}

TEST(SimulationRun, InvalidConfigIsRejected) {
    SimulationConfig c;
    c.meanLifetime = -1;
    ASSERT_ERROR(SimulationEngine{}.run(lineABC(), c), ErrorCode::InvalidArgument);
}

TEST(SimulationRun, TopologyWithoutLinksRejectsEverythingAsNoRoute) {
    SimulationConfig c;
    c.maxRequests = 50;
    c.endTime = 1e6;
    const auto r = must(SimulationEngine{}.run(makeTopology({1, 2, 3}, {}), c));
    EXPECT_EQ(r.metrics.totalRequests, 50u);
    EXPECT_EQ(r.metrics.acceptedRequests, 0u);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::NoRoute), 50u);
    EXPECT_DOUBLE_EQ(r.metrics.blockingRate, 1.0);
    EXPECT_EQ(r.metrics.totalCapacityChannels, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.0);
}

TEST(SimulationRun, RequestCapStopsTheRunAtTheLastEvent) {
    SimulationConfig c;
    c.seed = 5;
    c.maxRequests = 20;
    c.endTime = 1e9;  // far away: the cap, not the horizon, ends the run
    c.arrivalRate = 2.0;
    c.meanLifetime = 3.0;
    c.recordTrace = true;
    const auto r = must(SimulationEngine{}.run(lineABC(), c));
    EXPECT_EQ(r.metrics.totalRequests, 20u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 0u) << "all releases were processed";
    EXPECT_EQ(r.metrics.totalReleases, r.metrics.acceptedRequests);
    EXPECT_DOUBLE_EQ(r.metrics.duration, r.trace.back().time) << "the run ends at its last event";
    EXPECT_LT(r.metrics.duration, 1e9);
}

TEST(SimulationRun, HorizonEndsTheRunWhenTheCapIsNotReached) {
    SimulationConfig c;
    c.seed = 5;
    c.maxRequests = 1'000'000;
    c.endTime = 50.0;
    const auto r = must(SimulationEngine{}.run(lineABC(), c));
    EXPECT_DOUBLE_EQ(r.metrics.duration, 50.0);
    EXPECT_LT(r.metrics.totalRequests, 1'000'000u);
}

TEST(SimulationRun, SimulatedTimeIsVirtual) {
    // A billion simulated time units, but the run only costs the processing of its events.
    SimulationConfig c;
    c.seed = 3;
    c.endTime = 1e9;
    c.arrivalRate = 1e-5;  // ~10,000 requests
    c.meanLifetime = 1e4;
    const auto start = std::chrono::steady_clock::now();
    const auto r = must(SimulationEngine{}.run(grid(4, 8), c));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GT(r.metrics.totalRequests, 5000u);
    EXPECT_DOUBLE_EQ(r.metrics.duration, 1e9);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 20);
}

// ================================================================== determinism

TEST(SimulationDeterminism, SameSeedGivesIdenticalResultsIncludingTheFullTrace) {
    const Topology t = grid(5, 4);
    const auto first = must(SimulationEngine{}.run(t, gridConfig(12345)));
    const auto second = must(SimulationEngine{}.run(t, gridConfig(12345)));
    ASSERT_GT(first.trace.size(), 500u);
    EXPECT_EQ(first, second) << "request sequence, event order, outcomes and metrics must all be identical";
    EXPECT_EQ(first.seed, 12345u);
}

TEST(SimulationDeterminism, DifferentSeedsChangeTheWorkload) {
    const Topology t = grid(5, 4);
    const auto a = must(SimulationEngine{}.run(t, gridConfig(1)));
    const auto b = must(SimulationEngine{}.run(t, gridConfig(2)));
    EXPECT_NE(a.trace, b.trace);
    EXPECT_NE(a.metrics.totalRequests == b.metrics.totalRequests && a.metrics.acceptedRequests == b.metrics.acceptedRequests &&
                  a.metrics.averageUtilization == b.metrics.averageUtilization,
              true);
    std::size_t differingArrivals = 0;
    for (std::size_t i = 0; i < a.trace.size() && i < b.trace.size(); ++i) {
        if (a.trace[i].time != b.trace[i].time || a.trace[i].source != b.trace[i].source) ++differingArrivals;
    }
    EXPECT_GT(differingArrivals, 100u);
}

TEST(SimulationDeterminism, ReusingOneEngineObjectDoesNotLeakStateBetweenRuns) {
    const Topology t = grid(4, 4);
    const SimulationEngine engine;
    const auto first = must(engine.run(t, gridConfig(9)));
    (void)must(engine.run(t, gridConfig(10)));
    EXPECT_EQ(must(engine.run(t, gridConfig(9))), first);
}

TEST(SimulationDeterminism, WorkloadIsIdenticalOnNetworksWithDifferentCapacity) {
    // The arrival sequence (time, endpoints, demand) must not depend on how the network reacts.
    const auto roomy = must(SimulationEngine{}.run(grid(4, 64), gridConfig(77)));
    const auto tight = must(SimulationEngine{}.run(grid(4, 2), gridConfig(77)));
    std::vector<std::tuple<double, NodeId, NodeId, std::uint32_t>> a, b;
    for (const auto& e : roomy.trace)
        if (e.type == EventType::ConnectionArrival) a.emplace_back(e.time, e.source, e.destination, e.capacityChannels);
    for (const auto& e : tight.trace)
        if (e.type == EventType::ConnectionArrival) b.emplace_back(e.time, e.source, e.destination, e.capacityChannels);
    EXPECT_EQ(a, b);
    EXPECT_LT(tight.metrics.acceptanceRate, roomy.metrics.acceptanceRate);
}

// ================================================================== congestion and larger runs

TEST(SimulationRun, CongestedNetworkBlocksManyRequests) {
    const Topology t = grid(3, 2);
    SimulationConfig c;
    c.seed = 11;
    c.endTime = 400.0;
    c.arrivalRate = 5.0;
    c.meanLifetime = 20.0;  // offered load far above what 12 links x 2 channels can carry
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    const auto r = must(SimulationEngine{}.run(t, c));
    EXPECT_GT(r.metrics.blockingRate, 0.5);
    EXPECT_GT(r.metrics.acceptedRequests, 0u);
    EXPECT_GT(r.metrics.rejectionsByReason.at(ErrorCode::InsufficientCapacity), 0u);
    EXPECT_GT(r.metrics.averageUtilization, 0.5) << "a saturated network is mostly full";
    EXPECT_LE(r.metrics.averageUtilization, 1.0);
}

TEST(SimulationRun, LightLoadOnAGenerousNetworkBlocksNothing) {
    const Topology t = grid(4, 100);
    SimulationConfig c;
    c.seed = 11;
    c.endTime = 200.0;
    c.arrivalRate = 1.0;
    c.meanLifetime = 5.0;
    const auto r = must(SimulationEngine{}.run(t, c));
    EXPECT_GT(r.metrics.totalRequests, 100u);
    EXPECT_EQ(r.metrics.rejectedRequests, 0u);
    EXPECT_DOUBLE_EQ(r.metrics.acceptanceRate, 1.0);
    EXPECT_DOUBLE_EQ(r.metrics.blockingRate, 0.0);
}

TEST(SimulationRun, RoutingConfigurationIsAppliedToEveryRequest) {
    // Hop limit 1 on a line of three: A<->C (2 hops) can never be served, A<->B and B<->C can.
    SimulationConfig c;
    c.seed = 4;
    c.maxRequests = 300;
    c.endTime = 1e9;
    c.arrivalRate = 1.0;
    c.meanLifetime = 1.0;
    c.routing.maxHops = 1;
    c.recordTrace = true;
    const auto r = must(SimulationEngine{}.run(lineABC(), c));
    EXPECT_GT(r.metrics.acceptedRequests, 0u);
    EXPECT_GT(r.metrics.rejectionsByReason.at(ErrorCode::NoFeasibleRoute), 0u);
    EXPECT_EQ(r.metrics.maxHopCount, 1u);
    for (const auto& e : r.trace) {
        const bool farApart = (e.source == NodeId{1} && e.destination == NodeId{3}) || (e.source == NodeId{3} && e.destination == NodeId{1});
        if (e.type == EventType::ConnectionArrival) EXPECT_EQ(e.success, !farApart);
    }
}

TEST(SimulationRun, LargeSimulationOnAGridStaysConsistent) {
    const Topology t = grid(10, 8);  // 100 nodes, 180 links
    SimulationConfig c;
    c.seed = 2024;
    c.endTime = 1000.0;
    c.arrivalRate = 5.0;  // ~5000 requests
    c.meanLifetime = 10.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    const auto r = must(SimulationEngine{}.run(t, c));
    const SimulationMetrics& m = r.metrics;

    EXPECT_GT(m.totalRequests, 4500u);
    EXPECT_EQ(m.acceptedRequests + m.rejectedRequests, m.totalRequests);
    std::uint64_t rejectionSum = 0;
    for (const auto& [reason, count] : m.rejectionsByReason) rejectionSum += count;
    EXPECT_EQ(rejectionSum, m.rejectedRequests);
    EXPECT_EQ(m.eventsProcessed, m.totalRequests + m.totalReleases);
    EXPECT_EQ(m.activeConnectionsAtEnd, m.acceptedRequests - m.totalReleases);
    EXPECT_EQ(m.finalAllocatedChannels + m.finalAvailableChannels, m.totalCapacityChannels);
    EXPECT_LE(m.peakAllocatedChannels, m.totalCapacityChannels);
    EXPECT_GE(m.peakAllocatedChannels, m.finalAllocatedChannels);
    EXPECT_GT(m.averageUtilization, 0.0);
    EXPECT_LT(m.averageUtilization, 1.0);
    EXPECT_GE(m.averageHopCount, 1.0);
    EXPECT_GE(m.maxHopCount, static_cast<std::size_t>(m.averageHopCount));
    EXPECT_GT(m.acceptedRequests, 0u);
    EXPECT_GT(m.totalReleases, 1000u);
    EXPECT_DOUBLE_EQ(m.duration, 1000.0);
    EXPECT_DOUBLE_EQ(m.acceptanceRate + m.blockingRate, 1.0);

    // Recompute the final state independently from the trace: accepted and not yet released.
    std::map<ConnectionId, std::uint64_t> held;
    double lastTime = 0.0;
    for (const auto& e : r.trace) {
        EXPECT_GE(e.time, lastTime) << "the trace must be chronological";
        lastTime = e.time;
        if (e.type == EventType::ConnectionArrival && e.success) held[e.connection] = static_cast<std::uint64_t>(e.capacityChannels) * e.hops;
        if (e.type == EventType::ConnectionRelease) EXPECT_EQ(held.erase(e.connection), 1u) << "release of a connection that was not active";
    }
    std::uint64_t allocatedFromTrace = 0;
    for (const auto& [id, channels] : held) allocatedFromTrace += channels;
    EXPECT_EQ(held.size(), m.activeConnectionsAtEnd);
    EXPECT_EQ(allocatedFromTrace, m.finalAllocatedChannels);
}

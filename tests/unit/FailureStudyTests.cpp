#include <gtest/gtest.h>

#include <map>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/simulation/SimulationEngine.hpp"
#include "opticalnet/simulation/SimulationStudy.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

Topology grid(std::uint32_t side, std::uint32_t channels) {
    const auto node = [side](std::uint32_t r, std::uint32_t c) { return r * side + c + 1; };
    Topology t;
    for (std::uint32_t i = 1; i <= side * side; ++i) EXPECT_TRUE(t.addNode(makeNode(i)).ok());
    std::uint32_t link = 1;
    for (std::uint32_t r = 0; r < side; ++r)
        for (std::uint32_t c = 0; c < side; ++c) {
            if (c + 1 < side) {
                EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r, c + 1), 10, 1, LinkDirection::Bidirectional, channels)).ok());
            }
            if (r + 1 < side) {
                EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r + 1, c), 10, 1, LinkDirection::Bidirectional, channels)).ok());
            }
        }
    return t;
}

SimulationConfig scriptedFailureConfig() {
    SimulationConfig c;
    c.endTime = 100.0;
    c.arrivalRate = 3.0;
    c.meanLifetime = 15.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    // The same link fails and recovers in EVERY replication: if the overlay were shared between replications the
    // second one to fail it would find it "already failed".
    c.failureSchedule = {{20, FailureAction::FailLink, 3}, {40, FailureAction::RecoverLink, 3}, {50, FailureAction::FailNode, 7},
                         {70, FailureAction::RecoverNode, 7}};
    return c;
}

SimulationConfig randomConfig() {
    SimulationConfig c;
    c.endTime = 120.0;
    c.arrivalRate = 3.0;
    c.meanLifetime = 12.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    RandomFailureConfig f;
    f.failureRate = 0.15;
    f.meanRepairTime = 10.0;
    f.nodeFailureProbability = 0.2;
    c.randomFailures = f;
    return c;
}

StudyConfig study(std::uint64_t reps, std::size_t workers, std::uint64_t seed = 77) {
    StudyConfig s;
    s.replications = reps;
    s.workerCount = workers;
    s.baseSeed = seed;
    return s;
}

void expectSame(const StudyResult& a, const StudyResult& b, const std::string& what) {
    ASSERT_EQ(a.replications.size(), b.replications.size()) << what;
    for (std::size_t i = 0; i < a.replications.size(); ++i) EXPECT_EQ(a.replications[i], b.replications[i]) << what << ": replication " << i;
    EXPECT_EQ(a.aggregate, b.aggregate) << what;
}

}  // namespace

TEST(FailureStudy, ScriptedFailuresInEveryReplicationNeverCollide) {
    const Topology t = grid(4, 6);
    const auto result = must(SimulationStudy::run(t, scriptedFailureConfig(), study(12, 4)));
    ASSERT_EQ(result.replications.size(), 12u);
    for (const auto& r : result.replications) {
        EXPECT_EQ(r.metrics.linkFailures, 1u);
        EXPECT_EQ(r.metrics.nodeFailures, 1u);
        EXPECT_EQ(r.metrics.linkRecoveries, 1u);
        EXPECT_EQ(r.metrics.nodeRecoveries, 1u);
        EXPECT_EQ(r.metrics.failedLinksAtEnd, 0u);
        EXPECT_EQ(r.metrics.failedNodesAtEnd, 0u);
    }
    EXPECT_EQ(result.aggregate.totalFailureEvents, 24u);
}

TEST(FailureStudy, OneWorkerAndManyWorkersGiveIdenticalReplications) {
    const Topology t = grid(4, 6);
    const auto one = must(SimulationStudy::run(t, scriptedFailureConfig(), study(16, 1)));
    for (std::size_t workers : {2u, 4u, 8u, 0u}) {
        expectSame(one, must(SimulationStudy::run(t, scriptedFailureConfig(), study(16, workers))), std::to_string(workers) + " workers");
    }
}

TEST(FailureStudy, RandomFailuresAreDeterministicAcrossWorkerCounts) {
    const Topology t = grid(5, 5);
    const auto one = must(SimulationStudy::run(t, randomConfig(), study(16, 1)));
    EXPECT_GT(one.aggregate.totalFailureEvents, 16u);
    EXPECT_GT(one.aggregate.totalConnectionsAffected, 0u);
    for (std::size_t workers : {2u, 4u, 8u}) expectSame(one, must(SimulationStudy::run(t, randomConfig(), study(16, workers))), std::to_string(workers) + " workers");
}

TEST(FailureStudy, RepeatedParallelStudiesWithFailuresAreIdentical) {
    const Topology t = grid(4, 5);
    const auto first = must(SimulationStudy::run(t, randomConfig(), study(12, 4)));
    for (int run = 0; run < 5; ++run) expectSame(first, must(SimulationStudy::run(t, randomConfig(), study(12, 4))), "repeat " + std::to_string(run));
}

TEST(FailureStudy, EveryReplicationEqualsItsStandaloneRun) {
    const Topology t = grid(4, 5);
    const auto result = must(SimulationStudy::run(t, randomConfig(), study(10, 5)));
    for (std::uint64_t i = 0; i < 10; ++i) {
        auto direct = randomConfig();
        direct.seed = deriveReplicationSeed(77, i);
        EXPECT_EQ(result.replications[i], must(SimulationEngine{}.run(t, direct))) << "replication " << i;
    }
}

TEST(FailureStudy, ReplicationsDrawIndependentFailureSchedules) {
    const Topology t = grid(5, 5);
    const auto result = must(SimulationStudy::run(t, randomConfig(), study(12, 4)));
    std::set<std::uint64_t> failureCounts;
    std::set<double> firstFailureTimes;
    for (const auto& r : result.replications) {
        failureCounts.insert(r.metrics.totalFailureEvents);
        for (const auto& e : r.trace)
            if (e.kind == TraceKind::Event && (e.type == EventType::LinkFailure || e.type == EventType::NodeFailure)) {
                firstFailureTimes.insert(e.time);
                break;
            }
    }
    EXPECT_GT(firstFailureTimes.size(), 8u) << "not twelve copies of one schedule";
    EXPECT_GT(failureCounts.size(), 2u);
}

TEST(FailureStudy, TheSharedTopologyIsNeverModifiedByFailures) {
    const Topology t = grid(5, 5);
    const std::size_t nodes = t.nodeCount(), links = t.linkCount();
    (void)must(SimulationStudy::run(t, randomConfig(), study(16, 8)));
    EXPECT_EQ(t.nodeCount(), nodes);
    EXPECT_EQ(t.linkCount(), links);
    for (std::uint32_t l = 1; l <= links; ++l) ASSERT_TRUE(t.hasLink(LinkId{l}));
}

TEST(FailureStudy, AggregateFailureTotalsAddUp) {
    const Topology t = grid(5, 5);
    const auto result = must(SimulationStudy::run(t, randomConfig(), study(12, 4)));
    std::uint64_t events = 0, affected = 0, rerouted = 0, lost = 0;
    double downtime = 0.0;
    for (const auto& r : result.replications) {
        events += r.metrics.totalFailureEvents;
        affected += r.metrics.connectionsAffected;
        rerouted += r.metrics.connectionsRerouted;
        lost += r.metrics.connectionsLost;
        downtime += r.metrics.totalConnectionDowntime;
    }
    const StudyAggregate& a = result.aggregate;
    EXPECT_EQ(a.totalFailureEvents, events);
    EXPECT_EQ(a.totalConnectionsAffected, affected);
    EXPECT_EQ(a.totalConnectionsRerouted, rerouted);
    EXPECT_EQ(a.totalConnectionsLost, lost);
    EXPECT_EQ(affected, rerouted + lost);
    EXPECT_DOUBLE_EQ(a.totalConnectionDowntime, downtime);
    ASSERT_GT(affected, 0u);
    EXPECT_DOUBLE_EQ(a.aggregateRecoverySuccessRate, static_cast<double>(rerouted) / static_cast<double>(affected));
}

TEST(FailureStudy, AggregateWithoutFailuresHasNoFailureFigures) {
    const Topology t = grid(3, 4);
    SimulationConfig c;
    c.endTime = 40;
    const auto result = must(SimulationStudy::run(t, c, study(4, 2)));
    EXPECT_EQ(result.aggregate.totalFailureEvents, 0u);
    EXPECT_DOUBLE_EQ(result.aggregate.aggregateRecoverySuccessRate, 0.0);
    EXPECT_DOUBLE_EQ(result.aggregate.totalConnectionDowntime, 0.0);
}

TEST(FailureStudy, InvalidScheduleFailsTheWholeStudy) {
    const Topology t = grid(3, 4);
    auto c = scriptedFailureConfig();
    c.failureSchedule = {{5, FailureAction::FailLink, 9999}};
    const auto r = SimulationStudy::run(t, c, study(4, 2));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::NotFound);
    EXPECT_NE(r.error().message.find("replication 0"), std::string::npos) << "the lowest failing replication is reported";
}

// ============================================================ large failure study

TEST(FailureStudyLarge, TwelveReplicationsOnAnEightByEightGridWithFailuresAndMultipleWorkers) {
    const Topology t = grid(8, 6);  // 64 nodes, 112 links
    SimulationConfig c;
    c.endTime = 200.0;
    c.arrivalRate = 4.0;  // ~800 requests per replication
    c.meanLifetime = 15.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    RandomFailureConfig f;
    f.failureRate = 0.25;
    f.meanRepairTime = 15.0;
    f.nodeFailureProbability = 0.15;
    c.randomFailures = f;

    const auto sequential = must(SimulationStudy::run(t, c, study(12, 1, 2025)));
    const auto parallel = must(SimulationStudy::run(t, c, study(12, 0, 2025)));
    expectSame(sequential, parallel, "1 worker vs automatic");

    EXPECT_GT(parallel.aggregate.totalRequests, 12u * 500u);
    EXPECT_GT(parallel.aggregate.totalFailureEvents, 12u * 10u);
    EXPECT_GT(parallel.aggregate.totalConnectionsAffected, 50u);
    EXPECT_GT(parallel.aggregate.totalConnectionsRerouted, 0u);

    for (std::size_t i = 0; i < parallel.replications.size(); ++i) {
        const SimulationResult& r = parallel.replications[i];
        const SimulationMetrics& m = r.metrics;
        EXPECT_EQ(m.connectionsAffected, m.connectionsRerouted + m.connectionsLost) << i;
        EXPECT_EQ(m.finalAllocatedChannels + m.finalAvailableChannels, m.totalCapacityChannels) << i;
        EXPECT_EQ(m.failedLinksAtEnd, m.linkFailures - m.linkRecoveries) << i;
        EXPECT_EQ(m.failedNodesAtEnd, m.nodeFailures - m.nodeRecoveries) << i;
        EXPECT_EQ(m.acceptedRequests, m.activeConnectionsAtEnd + m.totalReleases + m.connectionsLost) << i << ": every accepted connection is still active, was released, or was lost";

        // Independent recomputation of the final state from the trace (no leaked or phantom connections).
        std::map<ConnectionId, std::uint64_t> held;
        for (const auto& e : r.trace) {
            if (e.kind == TraceKind::Event && e.type == EventType::ConnectionArrival && e.success) held[e.connection] = static_cast<std::uint64_t>(e.capacityChannels) * e.hops;
            if (e.kind == TraceKind::Event && e.type == EventType::ConnectionRelease) {
                EXPECT_EQ(held.erase(e.connection), 1u);
            }
            if (e.kind == TraceKind::Rerouted) held[e.connection] = static_cast<std::uint64_t>(e.capacityChannels) * e.hops;
            if (e.kind == TraceKind::Lost) {
                EXPECT_EQ(held.erase(e.connection), 1u);
            }
        }
        std::uint64_t channels = 0;
        for (const auto& [id, n] : held) channels += n;
        EXPECT_EQ(channels, m.finalAllocatedChannels) << i;
        EXPECT_EQ(held.size(), m.activeConnectionsAtEnd) << i;
    }
}

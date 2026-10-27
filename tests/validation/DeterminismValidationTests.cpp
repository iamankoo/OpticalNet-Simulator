#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

SimulationConfig config(std::uint64_t seed, bool failures) {
    SimulationConfig c;
    c.seed = seed;
    c.endTime = 100.0;
    c.arrivalRate = 3.0;
    c.meanLifetime = 10.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    if (failures) {
        RandomFailureConfig f;
        f.failureRate = 0.12;
        f.meanRepairTime = 10.0;
        f.nodeFailureProbability = 0.25;
        c.randomFailures = f;
    }
    return c;
}

StudyConfig study(std::uint64_t replications, std::size_t workers, std::uint64_t seed = 31337) {
    StudyConfig s;
    s.replications = replications;
    s.workerCount = workers;
    s.baseSeed = seed;
    return s;
}

std::vector<TraceEntry> failureEntries(const SimulationResult& r) {
    std::vector<TraceEntry> out;
    for (const TraceEntry& e : r.trace)
        if (e.kind == TraceKind::Event && (e.type == EventType::LinkFailure || e.type == EventType::NodeFailure || e.type == EventType::LinkRecovery ||
                                           e.type == EventType::NodeRecovery))
            out.push_back(e);
    return out;
}

}  // namespace

TEST(ValDeterminism, SameTopologyConfigAndSeedGiveTheSameEverything) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        for (bool failures : {false, true}) {
            const auto a = must(SimulationEngine{}.run(t, config(seed, failures)));
            const auto b = must(SimulationEngine{}.run(t, config(seed, failures)));
            EXPECT_EQ(a.trace, b.trace) << "request sequence, event order, paths (hops) and outcomes";
            EXPECT_EQ(a.metrics, b.metrics);
            EXPECT_EQ(a, b);
            EXPECT_EQ(failureEntries(a), failureEntries(b)) << "the failure sequence";
        }
    }
}

TEST(ValDeterminism, TheSameStudyGivesTheSameResultsForEveryWorkerCount) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{6, 10, 1.2, 0.2, 0.2, 20, 9, 3, 8, true}, 9);
    for (bool failures : {false, true}) {
        SCOPED_TRACE(failures ? "with random failures" : "without failures");
        const auto one = must(SimulationStudy::run(t, config(0, failures), study(20, 1)));
        for (std::size_t workers : {2u, 4u, 0u, 64u}) {
            const auto many = must(SimulationStudy::run(t, config(0, failures), study(20, workers)));
            ASSERT_EQ(many.replications.size(), one.replications.size());
            for (std::size_t i = 0; i < one.replications.size(); ++i) EXPECT_EQ(many.replications[i], one.replications[i]) << workers << " workers, replication " << i;
            EXPECT_EQ(many.aggregate, one.aggregate) << workers << " workers";
        }
    }
}

TEST(ValDeterminism, RepeatedParallelStudiesAreIdentical) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 21);
    const auto first = must(SimulationStudy::run(t, config(0, true), study(12, 4)));
    for (int run = 0; run < 6; ++run) {
        const auto again = must(SimulationStudy::run(t, config(0, true), study(12, 4)));
        EXPECT_EQ(again.replications, first.replications) << "run " << run;
        EXPECT_EQ(again.aggregate, first.aggregate);
    }
}

TEST(ValDeterminism, ReplicationsMatchTheirStandaloneRuns) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 22);
    const auto result = must(SimulationStudy::run(t, config(0, true), study(10, 5)));
    for (std::uint64_t i = 0; i < 10; ++i) {
        SimulationConfig direct = config(0, true);
        direct.seed = deriveReplicationSeed(31337, i);
        EXPECT_EQ(result.replications[i], must(SimulationEngine{}.run(t, direct))) << "replication " << i;
        ASSERT_VALID(checkSimulationTrace(t, result.replications[i]));
    }
}

TEST(ValDeterminism, ReplicationSeedsAreDistinctAndDoNotDependOnTheWorkerCount) {
    std::set<std::uint64_t> seeds;
    for (std::uint64_t i = 0; i < 5000; ++i) seeds.insert(deriveReplicationSeed(31337, i));
    EXPECT_EQ(seeds.size(), 5000u);
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 23);
    for (std::size_t workers : {1u, 3u, 8u}) {
        const auto r = must(SimulationStudy::run(t, config(0, false), study(12, workers)));
        for (std::uint64_t i = 0; i < 12; ++i) EXPECT_EQ(r.replications[i].seed, deriveReplicationSeed(31337, i));
    }
}

TEST(ValDeterminism, DifferentSeedsAreAllowedToDifferButWorkloadsAreSeedControlled) {
    // Not an assertion that every pair of seeds differs: only that the seed is what controls the workload.
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 24);
    const auto a = must(SimulationEngine{}.run(t, config(1, false)));
    const auto a2 = must(SimulationEngine{}.run(t, config(1, false)));
    const auto b = must(SimulationEngine{}.run(t, config(2, false)));
    EXPECT_EQ(a.trace, a2.trace);
    EXPECT_NE(a.trace, b.trace);
}

TEST(ValDeterminism, FailureScheduleDoesNotChangeTheRequestWorkload) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 25);
    const auto plain = must(SimulationEngine{}.run(t, config(7, false)));
    const auto damaged = must(SimulationEngine{}.run(t, config(7, true)));
    const auto requests = [](const SimulationResult& r) {
        std::vector<std::tuple<double, ConnectionId, NodeId, NodeId, std::uint32_t>> v;
        for (const TraceEntry& e : r.trace)
            if (e.kind == TraceKind::Event && e.type == EventType::ConnectionArrival) v.emplace_back(e.time, e.connection, e.source, e.destination, e.capacityChannels);
        return v;
    };
    EXPECT_EQ(requests(plain), requests(damaged));
}

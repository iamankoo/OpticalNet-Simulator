#include <gtest/gtest.h>

#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>
#include <thread>

#include "ConcurrencyTestUtils.hpp"
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
    for (std::uint32_t r = 0; r < side; ++r) {
        for (std::uint32_t c = 0; c < side; ++c) {
            if (c + 1 < side) EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r, c + 1), 10, 1, LinkDirection::Bidirectional, channels)).ok());
            if (r + 1 < side) EXPECT_TRUE(t.addLink(routedLink(link++, node(r, c), node(r + 1, c), 10, 1, LinkDirection::Bidirectional, channels)).ok());
        }
    }
    return t;
}

SimulationConfig baseConfig() {
    SimulationConfig c;
    c.endTime = 80.0;
    c.arrivalRate = 3.0;
    c.meanLifetime = 8.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    return c;
}

StudyConfig study(std::uint64_t replications, std::size_t workers, std::uint64_t seed = 12345) {
    StudyConfig s;
    s.replications = replications;
    s.workerCount = workers;
    s.baseSeed = seed;
    return s;
}

StudyResult runStudy(const Topology& t, const SimulationConfig& c, const StudyConfig& s) {
    return must(SimulationStudy::run(t, c, s));
}

void expectSameOutcome(const StudyResult& a, const StudyResult& b, const std::string& what) {
    ASSERT_EQ(a.replications.size(), b.replications.size()) << what;
    for (std::size_t i = 0; i < a.replications.size(); ++i) EXPECT_EQ(a.replications[i], b.replications[i]) << what << ": replication " << i;
    EXPECT_EQ(a.aggregate, b.aggregate) << what;
    EXPECT_TRUE(a.sameOutcome(b)) << what;
}

// A synthetic replication result with chosen metrics (for exact aggregation checks).
SimulationResult synthetic(std::uint64_t seed, std::uint64_t total, std::uint64_t accepted, std::map<ErrorCode, std::uint64_t> reasons,
                           std::uint64_t activeAtEnd, std::uint64_t peakActive, std::uint64_t peakAllocated, std::uint64_t finalAllocated,
                           double utilization, double avgCost, double avgHops, std::uint64_t releases) {
    SimulationResult r;
    r.seed = seed;
    SimulationMetrics& m = r.metrics;
    m.totalRequests = total;
    m.acceptedRequests = accepted;
    m.rejectedRequests = total - accepted;
    m.rejectionsByReason = std::move(reasons);
    m.acceptanceRate = total ? static_cast<double>(accepted) / static_cast<double>(total) : 0.0;
    m.blockingRate = total ? static_cast<double>(total - accepted) / static_cast<double>(total) : 0.0;
    m.activeConnectionsAtEnd = activeAtEnd;
    m.peakActiveConnections = peakActive;
    m.peakAllocatedChannels = peakAllocated;
    m.finalAllocatedChannels = finalAllocated;
    m.averageUtilization = utilization;
    m.averagePathCost = avgCost;
    m.averageHopCount = avgHops;
    m.totalReleases = releases;
    return r;
}

}  // namespace

// ============================================================ seed derivation

TEST(ReplicationSeeds, AreDeterministic) {
    for (std::uint64_t i = 0; i < 1000; ++i) EXPECT_EQ(deriveReplicationSeed(12345, i), deriveReplicationSeed(12345, i));
}

TEST(ReplicationSeeds, DifferForEveryReplicationIndex) {
    std::set<std::uint64_t> seeds;
    for (std::uint64_t i = 0; i < 100000; ++i) seeds.insert(deriveReplicationSeed(12345, i));
    EXPECT_EQ(seeds.size(), 100000u) << "the derivation is injective in the index";
}

TEST(ReplicationSeeds, DependOnTheReplicationIndexNotJustTheBaseSeed) {
    EXPECT_NE(deriveReplicationSeed(7, 0), deriveReplicationSeed(7, 1));
    EXPECT_NE(deriveReplicationSeed(7, 0), 7u);
    EXPECT_NE(deriveReplicationSeed(7, 1), 8u) << "not naive base + index";
}

TEST(ReplicationSeeds, DifferentBaseSeedsGiveDifferentSequences) {
    std::size_t equal = 0;
    for (std::uint64_t i = 0; i < 1000; ++i) equal += deriveReplicationSeed(1, i) == deriveReplicationSeed(2, i);
    EXPECT_EQ(equal, 0u);
    std::set<std::uint64_t> a, b;
    for (std::uint64_t i = 0; i < 1000; ++i) {
        a.insert(deriveReplicationSeed(1, i));
        b.insert(deriveReplicationSeed(2, i));
    }
    std::vector<std::uint64_t> common;
    for (auto s : a)
        if (b.contains(s)) common.push_back(s);
    EXPECT_TRUE(common.empty());
}

TEST(ReplicationSeeds, NeighbouringSeedsAreWellMixed) {
    // Adjacent indices must not give nearly-equal seeds (a weak mixer would): check that about half the bits differ.
    int totalDifferingBits = 0;
    for (std::uint64_t i = 0; i < 200; ++i) {
        const std::uint64_t x = deriveReplicationSeed(1, i) ^ deriveReplicationSeed(1, i + 1);
        totalDifferingBits += static_cast<int>(std::bitset<64>(x).count());
    }
    EXPECT_NEAR(totalDifferingBits / 200.0, 32.0, 4.0);
}

// ============================================================== study set-up

TEST(SimulationStudy, ZeroReplicationsIsRejected) {
    const Topology t = grid(3, 4);
    ASSERT_ERROR(SimulationStudy::run(t, baseConfig(), study(0, 2)), ErrorCode::InvalidArgument);
}

TEST(SimulationStudy, TooManyReplicationsIsRejected) {
    const Topology t = grid(3, 4);
    ASSERT_ERROR(SimulationStudy::run(t, baseConfig(), study(StudyConfig::kMaxReplications + 1, 2)), ErrorCode::InvalidArgument);
}

TEST(SimulationStudy, InvalidSimulationConfigIsRejectedBeforeAnyWork) {
    const Topology t = grid(3, 4);
    auto c = baseConfig();
    c.meanLifetime = 0.0;
    ASSERT_ERROR(SimulationStudy::run(t, c, study(4, 2)), ErrorCode::InvalidArgument);
}

TEST(SimulationStudy, WorkerCountResolution) {
    EXPECT_EQ(SimulationStudy::resolveWorkerCount(1, 20), 1u);
    EXPECT_EQ(SimulationStudy::resolveWorkerCount(2, 20), 2u);
    EXPECT_EQ(SimulationStudy::resolveWorkerCount(8, 20), 8u);
    EXPECT_EQ(SimulationStudy::resolveWorkerCount(64, 3), 3u) << "never more threads than replications";
    EXPECT_EQ(SimulationStudy::resolveWorkerCount(1'000'000, 5), 5u);
    EXPECT_EQ(SimulationStudy::resolveWorkerCount(4, 1), 1u);
    const std::size_t automatic = SimulationStudy::resolveWorkerCount(0, 1'000'000);
    EXPECT_GE(automatic, 1u) << "0 means automatic: at least one worker whatever the machine";
    EXPECT_LE(automatic, std::max<std::size_t>(std::thread::hardware_concurrency(), 1));
    EXPECT_EQ(SimulationStudy::resolveWorkerCount(0, 1), 1u);
}

TEST(SimulationStudy, WorkersUsedIsReportedAndBounded) {
    const Topology t = grid(3, 4);
    auto c = baseConfig();
    c.endTime = 20;
    EXPECT_EQ(runStudy(t, c, study(10, 4)).workersUsed, 4u);
    EXPECT_EQ(runStudy(t, c, study(3, 64)).workersUsed, 3u);
    EXPECT_EQ(runStudy(t, c, study(1, 8)).workersUsed, 1u);
    const auto automatic = runStudy(t, c, study(6, 0)).workersUsed;
    EXPECT_GE(automatic, 1u);
    EXPECT_LE(automatic, 6u);
}

// =============================================== replications and their results

TEST(SimulationStudy, SingleReplicationEqualsADirectRunWithTheDerivedSeed) {
    const Topology t = grid(4, 4);
    const auto result = runStudy(t, baseConfig(), study(1, 1, 99));
    ASSERT_EQ(result.replications.size(), 1u);
    auto direct = baseConfig();
    direct.seed = deriveReplicationSeed(99, 0);
    EXPECT_EQ(result.replications[0], must(SimulationEngine{}.run(t, direct)));
    EXPECT_EQ(result.baseSeed, 99u);
}

TEST(SimulationStudy, EveryReplicationEqualsItsStandaloneRun_NoCrossContamination) {
    const Topology t = grid(4, 4);
    const auto result = runStudy(t, baseConfig(), study(20, 4));
    ASSERT_EQ(result.replications.size(), 20u);
    for (std::uint64_t i = 0; i < 20; ++i) {
        auto direct = baseConfig();
        direct.seed = deriveReplicationSeed(12345, i);
        // A standalone single-threaded Phase 5 run: identical, so no replication saw another's state.
        EXPECT_EQ(result.replications[i], must(SimulationEngine{}.run(t, direct))) << "replication " << i;
    }
}

TEST(SimulationStudy, ResultsAreOrderedByReplicationIndexNotCompletionOrder) {
    const Topology t = grid(4, 4);
    auto c = baseConfig();
    c.recordTrace = false;
    const auto result = runStudy(t, c, study(30, 8));
    ASSERT_EQ(result.replications.size(), 30u);
    for (std::uint64_t i = 0; i < 30; ++i) EXPECT_EQ(result.replications[i].seed, deriveReplicationSeed(12345, i)) << i;
}

TEST(SimulationStudy, OrderingHoldsEvenWhenLaterReplicationsFinishFirst) {
    // Replication 0 waits until every later replication has finished, so completion order is the reverse of
    // index order. The results must still come back by index.
    constexpr std::uint64_t kReplications = 6;
    std::atomic<std::uint64_t> finishedOthers{0};
    const auto result = must(SimulationStudy::runReplications(study(kReplications, 4), [&](std::uint64_t index, std::uint64_t seed) -> Result<SimulationResult> {
        if (index == 0) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (finishedOthers.load() < kReplications - 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        } else {
            finishedOthers.fetch_add(1);
        }
        SimulationResult r;
        r.seed = seed;
        r.metrics.totalRequests = index;
        return r;
    }));
    ASSERT_EQ(result.replications.size(), kReplications);
    for (std::uint64_t i = 0; i < kReplications; ++i) {
        EXPECT_EQ(result.replications[i].metrics.totalRequests, i);
        EXPECT_EQ(result.replications[i].seed, deriveReplicationSeed(12345, i));
    }
}

// ====================================================================== determinism

TEST(SimulationStudyDeterminism, OneTwoFourAndEightWorkersGiveIdenticalReplications) {
    const Topology t = grid(5, 4);
    const auto one = runStudy(t, baseConfig(), study(20, 1));
    ASSERT_EQ(one.replications.size(), 20u);
    for (std::size_t workers : {2u, 4u, 8u}) {
        const auto many = runStudy(t, baseConfig(), study(20, workers));
        EXPECT_EQ(many.workersUsed, workers);
        expectSameOutcome(one, many, std::to_string(workers) + " workers vs 1");
    }
}

TEST(SimulationStudyDeterminism, AutomaticWorkerCountGivesTheSameResultsToo) {
    const Topology t = grid(5, 4);
    expectSameOutcome(runStudy(t, baseConfig(), study(12, 1)), runStudy(t, baseConfig(), study(12, 0)), "automatic workers");
}

TEST(SimulationStudyDeterminism, MoreWorkersThanReplicationsGivesTheSameResults) {
    const Topology t = grid(5, 4);
    const auto reference = runStudy(t, baseConfig(), study(3, 1));
    expectSameOutcome(reference, runStudy(t, baseConfig(), study(3, 64)), "64 workers, 3 replications");
    expectSameOutcome(reference, runStudy(t, baseConfig(), study(3, 100000)), "100000 workers, 3 replications");
}

TEST(SimulationStudyDeterminism, RepeatedParallelStudiesAreIdentical) {
    const Topology t = grid(5, 4);
    const auto first = runStudy(t, baseConfig(), study(16, 4));
    for (int run = 0; run < 10; ++run) expectSameOutcome(first, runStudy(t, baseConfig(), study(16, 4)), "repeat " + std::to_string(run));
    const auto eight = runStudy(t, baseConfig(), study(16, 8));
    for (int run = 0; run < 3; ++run) expectSameOutcome(eight, runStudy(t, baseConfig(), study(16, 8)), "8-worker repeat " + std::to_string(run));
    expectSameOutcome(first, eight, "4 vs 8 workers");
}

TEST(SimulationStudyDeterminism, TheSimulationConfigSeedIsIgnoredInFavourOfTheBaseSeed) {
    const Topology t = grid(4, 4);
    auto a = baseConfig();
    auto b = baseConfig();
    a.seed = 1;
    b.seed = 987654321;
    expectSameOutcome(runStudy(t, a, study(6, 3)), runStudy(t, b, study(6, 3)), "different SimulationConfig::seed");
}

TEST(SimulationStudyDeterminism, DifferentBaseSeedsGiveDifferentStudies) {
    const Topology t = grid(4, 4);
    const auto a = runStudy(t, baseConfig(), study(6, 3, 1));
    const auto b = runStudy(t, baseConfig(), study(6, 3, 2));
    EXPECT_FALSE(a.sameOutcome(b));
    EXPECT_NE(a.replications[0].trace, b.replications[0].trace);
}

// ====================================================== independence of replications

TEST(SimulationStudyIndependence, ReplicationsHaveDistinctSeedsAndDistinctWorkloads) {
    const Topology t = grid(4, 4);
    const auto result = runStudy(t, baseConfig(), study(20, 4));
    std::set<std::uint64_t> seeds;
    std::set<double> firstArrivals;
    std::set<std::uint64_t> requestCounts;
    for (const auto& r : result.replications) {
        seeds.insert(r.seed);
        ASSERT_FALSE(r.trace.empty());
        firstArrivals.insert(r.trace.front().time);
        requestCounts.insert(r.metrics.totalRequests);
    }
    EXPECT_EQ(seeds.size(), 20u);
    EXPECT_EQ(firstArrivals.size(), 20u) << "every replication generated its own arrival sequence";
    EXPECT_GT(requestCounts.size(), 5u) << "the replications are not copies of one another";
}

TEST(SimulationStudyIndependence, EachReplicationStartsWithCleanResourcesAndEndsConsistently) {
    // Tiny capacity: every replication congests its own network. If any state were shared between concurrent
    // replications, a replication would see foreign allocations (rejecting its first request) or its own
    // bookkeeping would disagree with the trace.
    const Topology t = grid(3, 2);
    auto c = baseConfig();
    c.arrivalRate = 6.0;
    c.meanLifetime = 20.0;
    const auto result = runStudy(t, c, study(24, 8));
    for (std::size_t i = 0; i < result.replications.size(); ++i) {
        const SimulationResult& r = result.replications[i];
        ASSERT_FALSE(r.trace.empty());
        EXPECT_EQ(r.trace.front().type, EventType::ConnectionArrival);
        EXPECT_TRUE(r.trace.front().success) << "replication " << i << ": the very first request meets an empty network";
        EXPECT_GT(r.metrics.rejectedRequests, 0u) << "the network does get congested";

        std::map<ConnectionId, std::uint64_t> held;
        for (const auto& e : r.trace) {
            if (e.type == EventType::ConnectionArrival && e.success) held[e.connection] = static_cast<std::uint64_t>(e.capacityChannels) * e.hops;
            if (e.type == EventType::ConnectionRelease) EXPECT_EQ(held.erase(e.connection), 1u) << "replication " << i;
        }
        std::uint64_t expected = 0;
        for (const auto& [id, channels] : held) expected += channels;
        EXPECT_EQ(expected, r.metrics.finalAllocatedChannels) << "replication " << i;
        EXPECT_EQ(held.size(), r.metrics.activeConnectionsAtEnd) << "replication " << i;
        EXPECT_LE(r.metrics.peakAllocatedChannels, r.metrics.totalCapacityChannels);
    }
}

// ================================================================ actual concurrency

TEST(SimulationStudyConcurrency, ReplicationsRunAtTheSameTimeOnTheSharedTopology) {
    constexpr std::uint64_t kWorkers = 4;
    const Topology t = grid(5, 4);
    Rendezvous rendezvous(kWorkers);
    // Every replication first waits until all four are running together, then simulates on the SAME topology object.
    const auto result = must(SimulationStudy::runReplications(study(kWorkers, kWorkers), [&](std::uint64_t index, std::uint64_t seed) -> Result<SimulationResult> {
        (void)index;
        if (!rendezvous.arriveAndWait()) return Error{ErrorCode::InternalError, "replications did not run concurrently"};
        auto c = baseConfig();
        c.seed = seed;
        return SimulationEngine{}.run(t, c);
    }));
    ASSERT_EQ(result.replications.size(), kWorkers);
    for (std::uint64_t i = 0; i < kWorkers; ++i) {
        auto direct = baseConfig();
        direct.seed = deriveReplicationSeed(12345, i);
        EXPECT_EQ(result.replications[i], must(SimulationEngine{}.run(t, direct))) << "concurrent run " << i << " matches the sequential one";
    }
}

TEST(SimulationStudyConcurrency, ManyWorkersReadingTheSameTopologyRepeatedly) {
    const Topology t = grid(8, 4);
    const std::size_t linksBefore = t.linkCount();
    auto c = baseConfig();
    c.recordTrace = false;
    c.endTime = 50.0;
    const auto reference = runStudy(t, c, study(32, 1));
    for (int round = 0; round < 3; ++round) expectSameOutcome(reference, runStudy(t, c, study(32, 8)), "round " + std::to_string(round));
    EXPECT_EQ(t.linkCount(), linksBefore) << "the topology is untouched by a study";
    EXPECT_EQ(t.nodeCount(), 64u);
}

TEST(SimulationStudyConcurrency, IndependentTasksOnTheSameTopologyThroughTheThreadPoolDirectly) {
    const Topology t = grid(5, 4);
    std::vector<SimulationResult> sequential;
    for (std::uint64_t i = 0; i < 12; ++i) {
        auto c = baseConfig();
        c.seed = 1000 + i;
        sequential.push_back(must(SimulationEngine{}.run(t, c)));
    }
    std::vector<std::thread> threads;
    std::vector<SimulationResult> concurrent(12);
    for (std::uint64_t i = 0; i < 12; ++i) {
        threads.emplace_back([&, i] {
            auto c = baseConfig();
            c.seed = 1000 + i;
            concurrent[i] = must(SimulationEngine{}.run(t, c));
        });
    }
    for (auto& th : threads) th.join();
    for (std::size_t i = 0; i < 12; ++i) EXPECT_EQ(concurrent[i], sequential[i]) << i;
}

// ========================================================================= aggregation

TEST(StudyAggregate, ExactTotalsRatiosAndMeans) {
    const std::vector<SimulationResult> inputs = {
        synthetic(1, 10, 8, {{ErrorCode::InsufficientCapacity, 2}}, 1, 3, 20, 5, 0.5, 2.0, 2.0, 7),
        synthetic(2, 30, 15, {{ErrorCode::NoRoute, 5}, {ErrorCode::InsufficientCapacity, 10}}, 3, 5, 40, 15, 0.7, 4.0, 3.0, 12),
        synthetic(3, 0, 0, {}, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0),
    };
    const auto result = must(SimulationStudy::runReplications(study(3, 2), [&](std::uint64_t index, std::uint64_t) -> Result<SimulationResult> { return inputs[index]; }));
    const StudyAggregate& a = result.aggregate;

    EXPECT_EQ(a.totalRequests, 40u);
    EXPECT_EQ(a.totalAccepted, 23u);
    EXPECT_EQ(a.totalRejected, 17u);
    EXPECT_EQ(a.totalReleases, 19u);
    EXPECT_EQ(a.rejectionsByReason.at(ErrorCode::InsufficientCapacity), 12u);
    EXPECT_EQ(a.rejectionsByReason.at(ErrorCode::NoRoute), 5u);
    EXPECT_DOUBLE_EQ(a.aggregateAcceptanceRate, 23.0 / 40.0) << "weighted by requests: 0.575, not the mean of 0.8, 0.5 and 0";
    EXPECT_DOUBLE_EQ(a.aggregateBlockingRate, 17.0 / 40.0);
    EXPECT_NE(a.aggregateAcceptanceRate, (0.8 + 0.5 + 0.0) / 3.0);

    EXPECT_DOUBLE_EQ(a.meanActiveConnectionsAtEnd, 4.0 / 3.0);
    EXPECT_DOUBLE_EQ(a.meanPeakActiveConnections, 8.0 / 3.0);
    EXPECT_DOUBLE_EQ(a.meanPeakAllocatedChannels, 20.0);
    EXPECT_DOUBLE_EQ(a.meanFinalAllocatedChannels, 20.0 / 3.0);
    EXPECT_DOUBLE_EQ(a.meanAverageUtilization, (0.5 + 0.7 + 0.0) / 3.0);

    EXPECT_DOUBLE_EQ(a.averagePathCost, (2.0 * 8 + 4.0 * 15) / 23.0) << "weighted by accepted connections";
    EXPECT_DOUBLE_EQ(a.averageHopCount, (2.0 * 8 + 3.0 * 15) / 23.0);

    EXPECT_EQ(a.acceptanceRate, summarize(std::vector<double>{0.8, 0.5, 0.0}));
    EXPECT_EQ(a.acceptanceRate.count, 3u);
    EXPECT_EQ(a.averageUtilization, summarize(std::vector<double>{0.5, 0.7, 0.0}));
    EXPECT_DOUBLE_EQ(a.acceptanceRate.mean, 1.3 / 3.0);
}

TEST(StudyAggregate, ZeroRequestsEverywhereGivesZerosNotNaN) {
    const Topology t = grid(3, 4);
    auto c = baseConfig();
    c.arrivalRate = 0.0;
    const auto result = runStudy(t, c, study(5, 2));
    const StudyAggregate& a = result.aggregate;
    EXPECT_EQ(a.totalRequests, 0u);
    EXPECT_DOUBLE_EQ(a.aggregateAcceptanceRate, 0.0);
    EXPECT_DOUBLE_EQ(a.aggregateBlockingRate, 0.0);
    EXPECT_DOUBLE_EQ(a.averagePathCost, 0.0);
    EXPECT_DOUBLE_EQ(a.averageHopCount, 0.0);
    EXPECT_DOUBLE_EQ(a.meanAverageUtilization, 0.0);
    EXPECT_FALSE(std::isnan(a.acceptanceRate.standardError));
    EXPECT_EQ(a.acceptanceRate.count, 5u);
    EXPECT_EQ(a.acceptanceRate.stdDev, 0.0);
}

TEST(StudyAggregate, EmptyListAggregatesToDefaults) {
    EXPECT_EQ(aggregate({}), StudyAggregate{});
}

TEST(StudyAggregate, StudyTotalsEqualTheSumOverReplications) {
    const Topology t = grid(4, 3);
    const auto result = runStudy(t, baseConfig(), study(10, 4));
    std::uint64_t requests = 0, accepted = 0, rejected = 0, releases = 0;
    for (const auto& r : result.replications) {
        requests += r.metrics.totalRequests;
        accepted += r.metrics.acceptedRequests;
        rejected += r.metrics.rejectedRequests;
        releases += r.metrics.totalReleases;
    }
    EXPECT_EQ(result.aggregate.totalRequests, requests);
    EXPECT_EQ(result.aggregate.totalAccepted, accepted);
    EXPECT_EQ(result.aggregate.totalRejected, rejected);
    EXPECT_EQ(result.aggregate.totalReleases, releases);
    EXPECT_EQ(accepted + rejected, requests);
    EXPECT_DOUBLE_EQ(result.aggregate.aggregateAcceptanceRate, static_cast<double>(accepted) / static_cast<double>(requests));
    EXPECT_GT(result.aggregate.acceptanceRate.confidenceHalfWidth95, 0.0);
}

// ================================================================= error handling

TEST(SimulationStudyErrors, FailingReplicationFailsTheStudyWithTheLowestIndexAndOthersStillFinish) {
    for (std::size_t workers : {1u, 4u}) {
        std::atomic<int> executed{0};
        const auto result = SimulationStudy::runReplications(study(10, workers), [&](std::uint64_t index, std::uint64_t seed) -> Result<SimulationResult> {
            executed.fetch_add(1);
            if (index == 3 || index == 5) return Error{ErrorCode::NoRoute, "synthetic failure " + std::to_string(index)};
            SimulationResult r;
            r.seed = seed;
            return r;
        });
        ASSERT_FALSE(result.ok());
        EXPECT_EQ(result.error().code, ErrorCode::NoRoute);
        EXPECT_NE(result.error().message.find("replication 3: synthetic failure 3"), std::string::npos) << result.error().message;
        EXPECT_EQ(executed.load(), 10) << "no replication is cancelled, and none is still running when the call returns (workers=" << workers << ")";
    }
}

TEST(SimulationStudyErrors, ExceptionsAreReportedNotSwallowed) {
    const auto result = SimulationStudy::runReplications(study(6, 3), [](std::uint64_t index, std::uint64_t) -> Result<SimulationResult> {
        if (index == 2) throw std::runtime_error("boom");
        return SimulationResult{};
    });
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_NE(result.error().message.find("replication 2"), std::string::npos);
    EXPECT_NE(result.error().message.find("boom"), std::string::npos);
}

TEST(SimulationStudyErrors, NonStandardExceptionsAreReportedToo) {
    const auto result = SimulationStudy::runReplications(study(4, 2), [](std::uint64_t index, std::uint64_t) -> Result<SimulationResult> {
        if (index == 1) throw 42;
        return SimulationResult{};
    });
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_NE(result.error().message.find("replication 1"), std::string::npos);
}

TEST(SimulationStudyErrors, AFailedStudyLeavesNoThreadsBehindAndTheNextStudyWorks) {
    for (int i = 0; i < 20; ++i) {
        ASSERT_FALSE(SimulationStudy::runReplications(study(8, 4), [](std::uint64_t index, std::uint64_t) -> Result<SimulationResult> {
                         if (index % 2 == 1) throw std::runtime_error("x");
                         return SimulationResult{};
                     }).ok());
    }
    const Topology t = grid(3, 4);
    auto c = baseConfig();
    c.endTime = 30;
    EXPECT_TRUE(SimulationStudy::run(t, c, study(4, 4)).ok());
}

TEST(SimulationStudyErrors, TheReportedErrorIsTheSameForEveryWorkerCount) {
    std::string expected;
    for (std::size_t workers : {1u, 2u, 8u}) {
        const auto result = SimulationStudy::runReplications(study(12, workers), [](std::uint64_t index, std::uint64_t) -> Result<SimulationResult> {
            if (index >= 4) return Error{ErrorCode::InconsistentState, "late failure"};
            return SimulationResult{};
        });
        ASSERT_FALSE(result.ok());
        if (expected.empty()) expected = result.error().message;
        EXPECT_EQ(result.error().message, expected);
        EXPECT_NE(expected.find("replication 4"), std::string::npos);
    }
}

// ================================================================== larger study

TEST(SimulationStudyLarge, TwentyFourReplicationsOnASixtyFourNodeGrid) {
    const Topology t = grid(8, 8);  // 64 nodes, 112 links
    SimulationConfig c;
    c.endTime = 150.0;
    c.arrivalRate = 4.0;  // ~600 requests per replication
    c.meanLifetime = 10.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    StudyConfig s = study(24, 1, 2024);

    const auto t0 = std::chrono::steady_clock::now();
    const auto sequential = runStudy(t, c, s);
    const auto t1 = std::chrono::steady_clock::now();
    s.workerCount = 0;  // automatic
    const auto parallel = runStudy(t, c, s);
    const auto t2 = std::chrono::steady_clock::now();

    expectSameOutcome(sequential, parallel, "24 replications, 1 worker vs automatic");
    EXPECT_EQ(parallel.replications.size(), 24u);
    EXPECT_GT(parallel.aggregate.totalRequests, 24u * 400u);
    EXPECT_EQ(parallel.aggregate.totalAccepted + parallel.aggregate.totalRejected, parallel.aggregate.totalRequests);
    EXPECT_GT(parallel.aggregate.aggregateAcceptanceRate, 0.0);
    EXPECT_LT(parallel.aggregate.aggregateAcceptanceRate, 1.0);
    EXPECT_GT(parallel.aggregate.acceptanceRate.stdDev, 0.0) << "replications genuinely vary";
    const auto ms = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
    RecordProperty("sequential_ms", static_cast<int>(ms(t0, t1)));
    RecordProperty("parallel_ms", static_cast<int>(ms(t1, t2)));
    RecordProperty("workers_used", static_cast<int>(parallel.workersUsed));
}

#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

SimulationConfig generatedConfig(std::uint64_t seed, bool failures) {
    SimulationConfig c;
    c.seed = seed;
    c.endTime = 120.0;
    c.arrivalRate = 2.0 + static_cast<double>(seed % 4);
    c.meanLifetime = 6.0 + static_cast<double>(seed % 5) * 3.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 1 + static_cast<std::uint32_t>(seed % 3);
    c.arrivalTiming = seed % 7 == 0 ? Timing::Fixed : Timing::Exponential;
    c.recordTrace = true;
    c.recoveryPolicy = seed % 5 == 0 ? RecoveryPolicy::NoRecovery : RecoveryPolicy::Reroute;
    if (seed % 3 == 0) c.routing.metric = CostMetric::Distance;
    if (seed % 4 == 0) c.routing.maxHops = 3;
    if (failures) {
        RandomFailureConfig f;
        f.failureRate = 0.1;
        f.meanRepairTime = 12.0;
        f.nodeFailureProbability = 0.2;
        c.randomFailures = f;
    }
    return c;
}

}  // namespace

TEST(ValSimulation, GeneratedRunsAreReproducedByAnIndependentTraceReplay) {
    std::uint64_t requests = 0, rejected = 0, releases = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig cfg;
        cfg.minNodes = 4;
        cfg.maxNodes = 10;
        cfg.minCapacity = 2;
        cfg.maxCapacity = 5;
        const Topology t = generateRandomTopology(cfg, seed);
        const auto r = must(SimulationEngine{}.run(t, generatedConfig(seed, false)));
        ASSERT_VALID(checkSimulationTrace(t, r));
        requests += r.metrics.totalRequests;
        rejected += r.metrics.rejectedRequests;
        releases += r.metrics.totalReleases;
        EXPECT_EQ(r.metrics.connectionsAffected, 0u) << "no failures were scheduled";
    }
    EXPECT_GT(requests, 4000u);
    EXPECT_GT(rejected, 200u) << "capacity limits must actually be hit";
    EXPECT_GT(releases, 2000u);
}

TEST(ValSimulation, GeneratedRunsWithRandomFailuresAreReproducedByTheReplay) {
    std::uint64_t affected = 0, rerouted = 0, lost = 0, failureEvents = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig cfg;
        cfg.minNodes = 5;
        cfg.maxNodes = 12;
        cfg.minCapacity = 2;
        cfg.maxCapacity = 6;
        cfg.extraLinksPerNode = 1.5;
        const Topology t = generateRandomTopology(cfg, 200 + seed);
        const std::string before = fingerprint(t);
        const auto r = must(SimulationEngine{}.run(t, generatedConfig(seed, true)));
        ASSERT_VALID(checkSimulationTrace(t, r));
        EXPECT_EQ(fingerprint(t), before) << "the topology must be untouched by failures";
        affected += r.metrics.connectionsAffected;
        rerouted += r.metrics.connectionsRerouted;
        lost += r.metrics.connectionsLost;
        failureEvents += r.metrics.totalFailureEvents;
    }
    EXPECT_GT(failureEvents, 100u);
    EXPECT_GT(affected, 100u);
    EXPECT_GT(rerouted, 30u);
    EXPECT_GT(lost, 30u);
    EXPECT_EQ(affected, rerouted + lost);
}

TEST(ValSimulation, ScriptedRandomWorkloadsWithFailureScriptsAreReproducedByTheReplay) {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, 300 + seed);
        Rng rng(seed);
        std::vector<ScriptedArrival> script;
        double time = 0;
        for (const ConnectionRequest& request : generateRandomRequests(t, RandomRequestConfig{}, 80, seed)) {
            time += 0.5 + static_cast<double>(rng.uniformInt(0, 3));
            script.push_back({time, request, 1.0 + static_cast<double>(rng.uniformInt(0, 12))});
        }
        auto config = tracedConfig(100.0, generateRandomFailureScript(t, 25, 100.0, seed), seed % 4 == 0 ? RecoveryPolicy::NoRecovery : RecoveryPolicy::Reroute);
        const auto r = runScript(t, script, config);
        ASSERT_VALID(checkSimulationTrace(t, r));
        EXPECT_EQ(r.metrics.totalFailureEvents + r.metrics.linkRecoveries + r.metrics.nodeRecoveries,
                  static_cast<std::uint64_t>(std::count_if(config.failureSchedule.begin(), config.failureSchedule.end(),
                                                           [](const ScriptedFailure& f) { return f.time <= 100.0; })))
            << "every scheduled failure/recovery within the horizon is processed exactly once";
    }
}

TEST(ValSimulation, UtilizationIsTheTimeWeightedAllocationNotAnEventAverage) {
    // One link, 10 channels, one connection of 5 channels from t=2 to t=6 in a run of 10 time units:
    // 5 * 4 / (10 * 10) = 0.2. An average over events (0, 5, 0 -> 1.67 channels) would give a different number.
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 10)});
    const auto r = runScript(t, {scripted(2, 1, 1, 2, 5, 4)}, tracedConfig(10));
    EXPECT_DOUBLE_EQ(r.metrics.averageUtilization, 0.2);
    EXPECT_VALID(checkSimulationTrace(t, r));
}

TEST(ValSimulation, EventTimesNeverGoBackwardsAndSequencesAreDeterministic) {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        const auto a = must(SimulationEngine{}.run(t, generatedConfig(seed, true)));
        const auto b = must(SimulationEngine{}.run(t, generatedConfig(seed, true)));
        double previous = 0;
        for (const TraceEntry& e : a.trace) {
            ASSERT_GE(e.time, previous) << seedContext(seed);
            previous = e.time;
        }
        ASSERT_EQ(a.trace.size(), b.trace.size());
        EXPECT_EQ(a.trace, b.trace) << "the same inputs replay the same event order";
    }
}

TEST(ValSimulation, EveryAcceptedArrivalCorrespondsToARealConnection) {
    // Simulation -> ConnectionManager boundary: drive a ConnectionManager independently from the trace of a run and check
    // that it accepts exactly the connections the simulation accepted (no failures, so decisions depend only on capacity).
    for (std::uint64_t seed = 1; seed <= 15; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, 400 + seed);
        SimulationConfig c = generatedConfig(seed, false);
        c.recoveryPolicy = RecoveryPolicy::Reroute;
        const auto r = must(SimulationEngine{}.run(t, c));
        ConnectionManager replay(t);
        std::size_t checked = 0;
        for (const TraceEntry& e : r.trace) {
            if (e.type == EventType::ConnectionArrival && e.kind == TraceKind::Event) {
                const auto outcome = replay.establish(ConnectionRequest{e.connection, e.source, e.destination, e.capacityChannels, c.routing});
                ASSERT_EQ(outcome.ok(), e.success) << "arrival of connection " << e.connection.value() << " at t=" << e.time;
                if (outcome.ok()) EXPECT_EQ(outcome.value().path().hopCount(), e.hops);
                ++checked;
            } else if (e.type == EventType::ConnectionRelease && e.kind == TraceKind::Event) {
                ASSERT_OK(replay.release(e.connection));
            }
        }
        EXPECT_EQ(checked, r.metrics.totalRequests);
        EXPECT_EQ(replay.resources().totalAllocated(), r.metrics.finalAllocatedChannels);
        EXPECT_EQ(replay.activeCount(), r.metrics.activeConnectionsAtEnd);
    }
}

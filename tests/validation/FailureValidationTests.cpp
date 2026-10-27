#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(ValFailures, RandomWalkOfFailuresAgreesWithAnIndependentModel) {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        FailureState failures(t);
        std::set<std::uint32_t> modelLinks, modelNodes;  // the independent model
        Rng rng(seed + 99);
        std::vector<std::uint32_t> linkIdsAll, nodeIdsAll;
        for (const auto& e : t.network().links()) linkIdsAll.push_back(e.first.value());
        for (const auto& e : t.network().nodes()) nodeIdsAll.push_back(e.first.value());
        for (int step = 0; step < 120; ++step) {
            const bool node = rng.uniformInt(0, 3) == 0;
            const bool fail = rng.uniformInt(0, 1) == 0;
            // Occasionally use an id that does not exist: always NotFound, never a state change.
            const bool bogus = rng.uniformInt(0, 14) == 0;
            const std::uint32_t id = bogus ? 90000 : node ? nodeIdsAll[rng.uniformIndex(nodeIdsAll.size())] : linkIdsAll[rng.uniformIndex(linkIdsAll.size())];
            auto& model = node ? modelNodes : modelLinks;
            Result<void> actual = node ? (fail ? failures.failNode(NodeId{id}) : failures.recoverNode(NodeId{id}))
                                       : (fail ? failures.failLink(LinkId{id}) : failures.recoverLink(LinkId{id}));
            ErrorCode want = ErrorCode::InvalidTransition;
            bool ok;
            if (bogus) { ok = false; want = ErrorCode::NotFound; }
            else if (fail) { ok = model.insert(id).second; }
            else { ok = model.erase(id) == 1; }
            ASSERT_EQ(actual.ok(), ok) << "step " << step;
            if (!ok) {
                EXPECT_EQ(actual.error().code, want);
            }
            ASSERT_EQ(failures.failedLinks().size(), modelLinks.size());
            ASSERT_EQ(failures.failedNodes().size(), modelNodes.size());
            ASSERT_VALID(checkFailureState(failures, t));
        }
        failures.clearFailures();
        EXPECT_FALSE(failures.anyFailed());
        ASSERT_VALID(checkFailureState(failures, t));
    }
}

TEST(ValFailures, RecoveredResourcesBecomeAvailableAgain) {
    const Topology t = fixture("link_failure");
    FailureState f(t);
    for (std::uint32_t link = 1; link <= 4; ++link) {
        ASSERT_OK(f.failLink(LinkId{link}));
        EXPECT_FALSE(f.isLinkUsable(*t.findLink(LinkId{link})));
        ASSERT_OK(f.recoverLink(LinkId{link}));
        EXPECT_TRUE(f.isLinkUsable(*t.findLink(LinkId{link})));
    }
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_FALSE(f.isLinkUsable(*t.findLink(LinkId{1})));
    ASSERT_OK(f.recoverNode(NodeId{2}));
    EXPECT_TRUE(f.isLinkUsable(*t.findLink(LinkId{1})));
    ASSERT_VALID(checkFailureState(f, t));
}

TEST(ValFailures, RandomFailureScriptsPreserveEveryInvariantThroughASimulation) {
    std::uint64_t rerouted = 0, lost = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig cfg;
        cfg.minNodes = 5;
        cfg.maxNodes = 10;
        cfg.extraLinksPerNode = 1.5;
        cfg.minCapacity = 3;
        cfg.maxCapacity = 6;
        const Topology t = generateRandomTopology(cfg, 600 + seed);
        const std::string before = fingerprint(t);
        Rng rng(seed);
        std::vector<ScriptedArrival> script;
        double time = 0;
        for (const ConnectionRequest& request : generateRandomRequests(t, RandomRequestConfig{1, 2, 0.1}, 100, seed)) {
            time += 0.4 + static_cast<double>(rng.uniformInt(0, 2));
            script.push_back({time, request, 10.0 + static_cast<double>(rng.uniformInt(0, 25))});
        }
        const auto schedule = generateRandomFailureScript(t, 30, 80.0, seed);
        ASSERT_TRUE(validateFailureSchedule(t, schedule).ok()) << "the generator must only produce valid schedules";
        const auto result = runScript(t, script, tracedConfig(100.0, schedule));
        ASSERT_VALID(checkSimulationTrace(t, result));
        EXPECT_EQ(fingerprint(t), before);

        // Every rerouted or lost connection was first reported as affected by a failure.
        std::set<ConnectionId> affectedNow;
        for (const TraceEntry& e : result.trace) {
            if (e.kind == TraceKind::Affected) affectedNow.insert(e.connection);
            if (e.kind == TraceKind::Rerouted) {
                EXPECT_TRUE(affectedNow.contains(e.connection)) << "connection " << e.connection.value() << " rerouted without being affected";
                ++rerouted;
            }
            if (e.kind == TraceKind::Lost) {
                EXPECT_TRUE(affectedNow.contains(e.connection));
                ++lost;
            }
        }
        EXPECT_LE(affectedNow.size(), result.metrics.connectionsAffected) << "(a connection may be affected by several failures)";
    }
    EXPECT_GT(rerouted, 30u);
    EXPECT_GT(lost, 10u);
}

TEST(ValFailures, ScriptedFailuresReplayIdentically) {
    for (std::uint64_t seed = 1; seed <= 15; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, 700 + seed);
        const auto requests = generateRandomRequests(t, RandomRequestConfig{}, 40, seed);
        std::vector<ScriptedArrival> script;
        double time = 0;
        for (const auto& r : requests) script.push_back({time += 1.0, r, 15.0});
        const auto config = tracedConfig(60.0, generateRandomFailureScript(t, 20, 55.0, seed));
        EXPECT_EQ(runScript(t, script, config), runScript(t, script, config));
    }
}

TEST(ValFailures, NoFailureStateLeaksBetweenReplications) {
    // The same scripted link failure happens in every replication. Were the overlay shared, every replication after the
    // first would try to fail an already-failed link and the study would error out.
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 11);
    SimulationConfig c;
    c.endTime = 60;
    c.arrivalRate = 2.0;
    c.meanLifetime = 8.0;
    c.recordTrace = true;
    c.failureSchedule = {{10, FailureAction::FailLink, 1}, {30, FailureAction::RecoverLink, 1}, {35, FailureAction::FailNode, 2}};
    StudyConfig s;
    s.replications = 16;
    s.workerCount = 4;
    s.baseSeed = 5;
    const auto study = must(SimulationStudy::run(t, c, s));
    ASSERT_EQ(study.replications.size(), 16u);
    for (const auto& r : study.replications) {
        EXPECT_EQ(r.metrics.linkFailures, 1u);
        EXPECT_EQ(r.metrics.linkRecoveries, 1u);
        EXPECT_EQ(r.metrics.nodeFailures, 1u);
        EXPECT_EQ(r.metrics.failedNodesAtEnd, 1u);
        EXPECT_EQ(r.metrics.failedLinksAtEnd, 0u);
        ASSERT_VALID(checkSimulationTrace(t, r));
    }
}

TEST(ValFailures, LostConnectionsHaveNoAllocationAndAreNotActive) {
    // Single route: the failure must lose the connection, and the network must be empty and valid afterwards.
    const Topology t = fixture("simple_line");
    const auto r = runScript(t, {scripted(1, 1, 1, 4, 6, 100)}, tracedConfig(20, {{5, FailureAction::FailLink, 2}}));
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 0u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 0u);
    ASSERT_VALID(checkSimulationTrace(t, r));
}

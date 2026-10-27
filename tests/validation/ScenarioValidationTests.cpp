// End-to-end scenarios: each one runs a complete flow through several layers and validates it with the independent checkers.
#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// A ConnectionManager wired to a FailureState, the way the simulation engine wires it.
struct World {
    explicit World(const Topology& topology) : topology(topology), failures(topology), manager(topology) {
        manager.setAvailability([this](const FiberLink& l) { return failures.isLinkUsable(l); });
    }
    ValidationResult validate() const {
        ValidationResult r = checkConnections(manager, topology, &failures);
        r.merge(checkResources(manager.resources(), topology));
        r.merge(checkFailureState(failures, topology));
        r.merge(checkTopology(topology));
        return r;
    }
    // The simulation's reaction to a failure: detach everything affected, re-establish in id order.
    struct Outcome {
        std::vector<ConnectionId> rerouted, lost;
    };
    Outcome recover() {
        std::vector<Connection> victims;
        for (const auto& [id, c] : manager.activeConnections())
            if (failures.isPathAffected(c.path())) victims.push_back(c);
        for (const Connection& v : victims) EXPECT_TRUE(manager.detach(v.id()).ok());
        Outcome out;
        for (const Connection& v : victims) (manager.establish(v.request()).ok() ? out.rerouted : out.lost).push_back(v.id());
        return out;
    }
    const Topology& topology;
    FailureState failures;
    ConnectionManager manager;
};

ConnectionRequest request(std::uint32_t id, std::uint32_t from, std::uint32_t to, std::uint32_t demand) {
    return ConnectionRequest{ConnectionId{id}, NodeId{from}, NodeId{to}, demand, {}};
}

}  // namespace

// Scenario 1: topology -> generated requests -> routing -> allocation -> release -> metrics.
TEST(ValScenario, NormalOperationCompleteLifecycle) {
    const Topology ring = fixture("ring");  // 6 nodes, 10 channels per link
    SimulationConfig c;
    c.seed = 2;
    c.endTime = 1e6;  // far away: the request cap ends the run
    c.maxRequests = 60;
    c.arrivalRate = 1.0;
    c.meanLifetime = 2.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    const auto r = must(SimulationEngine{}.run(ring, c));
    const SimulationMetrics& m = r.metrics;
    EXPECT_EQ(m.totalRequests, 60u);
    EXPECT_EQ(m.acceptedRequests, 60u) << "light load on a generous ring: nothing is blocked";
    EXPECT_DOUBLE_EQ(m.acceptanceRate, 1.0);
    EXPECT_EQ(m.totalReleases, 60u) << "every connection lived out its lifetime and was released";
    EXPECT_EQ(m.activeConnectionsAtEnd, 0u);
    EXPECT_EQ(m.finalAllocatedChannels, 0u);
    EXPECT_GT(m.averageUtilization, 0.0);
    EXPECT_GE(m.averageHopCount, 1.0);
    ASSERT_VALID(checkSimulationTrace(ring, r));
}

// Scenario 2: congestion -> alternate route or rejection, never over-allocation.
TEST(ValScenario, CongestionUsesTheAlternateRouteThenRejects) {
    const Topology t = fixture("capacity_congestion");  // top route A-B-D: 4 channels, cost 2; bottom A-C-D: 10 channels, cost 4
    World w(t);
    EXPECT_EQ(must(w.manager.establish(request(1, 1, 4, 3))).path().nodes(), nodeIds({1, 2, 4})) << "the cheaper top route first";
    EXPECT_EQ(must(w.manager.establish(request(2, 1, 4, 3))).path().nodes(), nodeIds({1, 3, 4})) << "top has 1 free channel: alternate route";
    EXPECT_EQ(must(w.manager.establish(request(3, 1, 4, 5))).path().nodes(), nodeIds({1, 3, 4}));
    ASSERT_VALID(w.validate());
    const auto fourth = w.manager.establish(request(4, 1, 4, 5));
    ASSERT_FALSE(fourth.ok());
    EXPECT_EQ(fourth.error().code, ErrorCode::InsufficientCapacity) << "routes exist, but no free capacity";
    EXPECT_EQ(w.manager.resources().allocated(LinkId{3}), 8u);
    ASSERT_VALID(w.validate()) << "a rejection changes nothing";
    for (const auto& [id, usage] : w.manager.resources().usages()) EXPECT_LE(usage.allocated, usage.total);

    // The same flow as a full simulation, validated from its trace.
    const auto r = runScript(t, {scripted(1, 1, 1, 4, 3, 50), scripted(2, 2, 1, 4, 3, 50), scripted(3, 3, 1, 4, 5, 50), scripted(4, 4, 1, 4, 5, 50)},
                             tracedConfig(10));
    EXPECT_EQ(r.metrics.acceptedRequests, 3u);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::InsufficientCapacity), 1u);
    EXPECT_EQ(r.metrics.finalAllocatedChannels, 22u) << "3*2 + 3*2 + 5*2 channel-links";
    ASSERT_VALID(checkSimulationTrace(t, r));
}

// Scenario 3: active connection -> link failure -> detection -> reroute -> same id -> valid allocation.
TEST(ValScenario, LinkFailureIsDetectedAndTheConnectionRerouted) {
    const Topology t = fixture("link_failure");
    World w(t);
    ASSERT_OK(w.manager.establish(request(42, 1, 3, 4)));
    ASSERT_EQ(w.manager.find(ConnectionId{42})->path().nodes(), nodeIds({1, 2, 3}));
    ASSERT_VALID(w.validate());

    ASSERT_OK(w.failures.failLink(LinkId{2}));
    EXPECT_TRUE(w.failures.isPathAffected(w.manager.find(ConnectionId{42})->path())) << "detected";
    EXPECT_TRUE(w.validate().failed("connections.active_on_failed_resource")) << "until it is handled the state is (correctly) reported as inconsistent";
    const auto outcome = w.recover();
    EXPECT_EQ(outcome.rerouted, std::vector<ConnectionId>{ConnectionId{42}});
    EXPECT_TRUE(outcome.lost.empty());
    const Connection* moved = w.manager.find(ConnectionId{42});
    ASSERT_NE(moved, nullptr) << "same ConnectionId";
    EXPECT_EQ(moved->path().nodes(), nodeIds({1, 4, 3}));
    EXPECT_EQ(moved->capacityChannels(), 4u);
    ASSERT_VALID(w.validate());
    EXPECT_EQ(w.manager.resources().allocated(LinkId{1}), 0u) << "old links released";
    EXPECT_EQ(w.manager.resources().allocated(LinkId{3}), 4u);
}

// Scenario 4: node failure -> affected connections -> alternate route -> recovery.
TEST(ValScenario, NodeFailureAndRecovery) {
    const Topology t = fixture("node_failure");  // A-B-C (2 hops) and A-D-E-C (3 hops)
    World w(t);
    ASSERT_OK(w.manager.establish(request(1, 1, 3, 2)));
    ASSERT_OK(w.failures.failNode(NodeId{2}));
    EXPECT_EQ(w.recover().rerouted, std::vector<ConnectionId>{ConnectionId{1}});
    EXPECT_EQ(w.manager.find(ConnectionId{1})->path().nodes(), nodeIds({1, 4, 5, 3}));
    ASSERT_VALID(w.validate());
    ASSERT_FALSE(w.manager.establish(request(2, 1, 2, 1)).ok()) << "the failed node cannot be reached";
    EXPECT_EQ(must(w.manager.establish(request(3, 1, 3, 1))).path().nodes(), nodeIds({1, 4, 5, 3})) << "new routes avoid the failed node too";

    ASSERT_OK(w.failures.recoverNode(NodeId{2}));
    EXPECT_EQ(must(w.manager.establish(request(4, 1, 3, 1))).path().nodes(), nodeIds({1, 2, 3})) << "the recovered node is usable again";
    ASSERT_VALID(w.validate());
}

// Scenario 5: no feasible recovery -> the connection is lost, cleanly.
TEST(ValScenario, UnrecoverableConnectionIsLostWithoutLeaks) {
    const Topology t = fixture("simple_line");
    World w(t);
    ASSERT_OK(w.manager.establish(request(1, 1, 4, 6)));
    ASSERT_OK(w.manager.establish(request(2, 1, 2, 2)));  // unaffected neighbour on link 1
    ASSERT_OK(w.failures.failLink(LinkId{3}));
    const auto outcome = w.recover();
    EXPECT_TRUE(outcome.rerouted.empty());
    EXPECT_EQ(outcome.lost, std::vector<ConnectionId>{ConnectionId{1}});
    EXPECT_FALSE(w.manager.contains(ConnectionId{1})) << "a lost connection is not active";
    EXPECT_EQ(w.manager.resources().totalAllocated(), 2u) << "only the unaffected connection's channels remain";
    ASSERT_VALID(w.validate());

    const auto r = runScript(t, {scripted(1, 1, 1, 4, 6, 50), scripted(1.5, 2, 1, 2, 2, 50)}, tracedConfig(10, {{5, FailureAction::FailLink, 3}}));
    EXPECT_EQ(r.metrics.connectionsLost, 1u);
    EXPECT_EQ(r.metrics.activeConnectionsAtEnd, 1u);
    ASSERT_VALID(checkSimulationTrace(t, r));
}

// Scenario 6: a parallel study equals its single-worker execution, and each replication validates.
TEST(ValScenario, ParallelStudyEqualsSingleWorkerStudy) {
    const Topology ring = fixture("ring");
    SimulationConfig c;
    c.endTime = 80;
    c.arrivalRate = 3.0;
    c.meanLifetime = 8.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 3;
    c.recordTrace = true;
    c.failureSchedule = {{20, FailureAction::FailLink, 3}, {40, FailureAction::RecoverLink, 3}, {50, FailureAction::FailNode, 5}};
    StudyConfig s;
    s.replications = 12;
    s.baseSeed = 4;
    s.workerCount = 1;
    const auto one = must(SimulationStudy::run(ring, c, s));
    s.workerCount = 4;
    const auto four = must(SimulationStudy::run(ring, c, s));
    ASSERT_EQ(one.replications.size(), 12u);
    EXPECT_EQ(one.replications, four.replications);
    EXPECT_EQ(one.aggregate, four.aggregate);
    for (const auto& r : four.replications) ASSERT_VALID(checkSimulationTrace(ring, r));
    EXPECT_GT(four.aggregate.totalConnectionsAffected, 0u);
}

TEST(ValScenario, CongestionAndFailureTogetherKeepEverythingConsistent) {
    const Topology t = fixture("reroute");  // detour A-D-C has only 4 channels
    World w(t);
    ASSERT_OK(w.manager.establish(request(1, 1, 3, 4)));
    ASSERT_OK(w.manager.establish(request(2, 1, 3, 4)));
    ASSERT_OK(w.failures.failLink(LinkId{2}));
    const auto outcome = w.recover();
    EXPECT_EQ(outcome.rerouted, std::vector<ConnectionId>{ConnectionId{1}}) << "the lowest id takes the only detour capacity";
    EXPECT_EQ(outcome.lost, std::vector<ConnectionId>{ConnectionId{2}});
    ASSERT_VALID(w.validate());
    EXPECT_EQ(w.manager.resources().allocated(LinkId{3}), 4u);
}

// A reroute must honour the connection's original routing constraints; recovery may not relax them.
TEST(ValScenario, RoutingConstraintsSurviveRecovery) {
    const Topology t = fixture("node_failure");  // A-B-C is 2 hops, the detour A-D-E-C is 3 hops
    RoutingConstraints limited;
    limited.maxHops = 2;
    RoutingConstraints avoidE;
    avoidE.blockedNodes.insert(NodeId{5});
    RoutingConstraints cheap;
    cheap.maxCost = 2.5;
    const auto r = runScript(t,
                             {scripted(1, 1, 1, 3, 1, 100, limited),   // may never use more than 2 hops
                              scripted(1, 2, 1, 3, 1, 100, avoidE),    // may never touch node E
                              scripted(1, 3, 1, 3, 1, 100, cheap),     // may never cost more than 2.5
                              scripted(1, 4, 1, 3, 1, 100)},           // unconstrained
                             tracedConfig(10, {{5, FailureAction::FailNode, 2}}));
    EXPECT_EQ(r.metrics.connectionsAffected, 4u);
    EXPECT_EQ(r.metrics.connectionsRerouted, 1u) << "only the unconstrained connection can move to the 3-hop detour";
    EXPECT_EQ(r.metrics.connectionsLost, 3u);
    std::vector<std::uint32_t> rerouted;
    for (const TraceEntry& e : r.trace)
        if (e.kind == TraceKind::Rerouted) rerouted.push_back(e.connection.value());
    EXPECT_EQ(rerouted, std::vector<std::uint32_t>{4});
    ASSERT_VALID(checkSimulationTrace(t, r));
}

// Events that share a timestamp are processed as: recoveries, failures, releases, arrivals.
TEST(ValScenario, SameTimestampEventsAreProcessedInTheDocumentedOrder) {
    const Topology t = fixture("simple_line");  // links 1,2,3 with 10 channels each
    // #1 fills link 1 until t=5 (released exactly then); #2 arrives at t=5 wanting the whole link; at t=5 link 2 recovers and
    // link 3 fails. Link 2 failed at t=2.
    const auto r = runScript(t, {scripted(1, 1, 1, 2, 10, 4), scripted(5, 2, 1, 2, 10, 100)},
                             tracedConfig(10, {{2, FailureAction::FailLink, 2}, {5, FailureAction::RecoverLink, 2}, {5, FailureAction::FailLink, 3}}));
    std::vector<EventType> at5;
    for (const TraceEntry& e : r.trace)
        if (e.time == 5.0 && e.kind == TraceKind::Event) at5.push_back(e.type);
    EXPECT_EQ(at5, (std::vector<EventType>{EventType::LinkRecovery, EventType::LinkFailure, EventType::ConnectionRelease, EventType::ConnectionArrival}));
    EXPECT_EQ(r.metrics.acceptedRequests, 2u) << "the release at t=5 frees the link for the arrival at t=5";
    EXPECT_EQ(r.metrics.failedLinksAtEnd, 1u);
    ASSERT_VALID(checkSimulationTrace(t, r));

    // The validator itself enforces the order: swapping the release and the arrival in the trace is detected.
    SimulationResult swapped = r;
    std::size_t release = 0, arrival = 0;
    for (std::size_t i = 0; i < swapped.trace.size(); ++i) {
        if (swapped.trace[i].time != 5.0 || swapped.trace[i].kind != TraceKind::Event) continue;
        if (swapped.trace[i].type == EventType::ConnectionRelease) release = i;
        if (swapped.trace[i].type == EventType::ConnectionArrival) arrival = i;
    }
    ASSERT_GT(release, 0u);
    ASSERT_GT(arrival, release);
    std::swap(swapped.trace[release], swapped.trace[arrival]);
    EXPECT_DETECTED(checkSimulationTrace(t, swapped), "trace.same_time_order");
}

TEST(ValScenario, FailureIsProcessedBeforeAnArrivalAtTheSameInstant) {
    const Topology t = fixture("node_failure");
    const auto r = runScript(t, {scripted(5, 1, 1, 3, 1, 100)}, tracedConfig(10, {{5, FailureAction::FailLink, 2}}));
    ASSERT_GE(r.trace.size(), 2u);
    EXPECT_EQ(r.trace[0].type, EventType::LinkFailure);
    EXPECT_EQ(r.trace[1].type, EventType::ConnectionArrival);
    EXPECT_EQ(r.trace[1].hops, 3u) << "the arrival already sees the failed link and takes the detour";
    ASSERT_VALID(checkSimulationTrace(t, r));
}

// Cross-layer validation of Topology -> Routing -> ConnectionManager -> ResourceManager, with the failure overlay.
#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

struct Counts {
    int accepted = 0, rejected = 0, released = 0, failuresApplied = 0, affected = 0, rerouted = 0, lost = 0;
    int insufficient = 0, noFeasible = 0, noRoute = 0;
};

// Drives a ConnectionManager with random establish / release / fail / recover operations, mirroring what the simulation
// engine does on a failure (detach every affected connection, then re-establish each in id order), and validates every
// layer independently after every operation.
void drive(std::uint64_t seed, Counts& counts) {
    RandomTopologyConfig cfg;
    cfg.minNodes = 4;
    cfg.maxNodes = 7;
    cfg.minCapacity = 2;
    cfg.maxCapacity = 6;
    const Topology t = generateRandomTopology(cfg, seed);
    FailureState failures(t);
    ConnectionManager manager(t);
    manager.setAvailability([&failures](const FiberLink& l) { return failures.isLinkUsable(l); });
    ShadowLedger ledger(t);  // independent copy of the allocations the manager should hold
    Rng rng(seed * 6007 + 3);

    std::vector<NodeId> nodes;
    for (const auto& e : t.network().nodes()) nodes.push_back(e.first);
    std::vector<LinkId> links;
    for (const auto& e : t.network().links()) links.push_back(e.first);
    std::uint32_t nextId = 1;
    std::vector<ConnectionId> active;

    const auto establish = [&](const ConnectionRequest& request, bool isRecovery) {
        // Oracle: exhaustive search under the request's constraints, the failure overlay and the free capacity of the ledger.
        RoutingConstraints oracleConstraints = request.constraints;
        oracleConstraints.linkFilter = [&](const FiberLink& l) { return ledger.allocated(l.id()) + request.capacityChannels <= l.capacityChannels(); };
        const ReferenceRoute expected = exhaustiveRoute(t, request.source, request.destination, oracleConstraints, &failures);
        const auto result = manager.establish(request);
        const std::string context = "connection " + std::to_string(request.id.value()) + " " + std::to_string(request.source.value()) + "->" +
                                    std::to_string(request.destination.value()) + " demand " + std::to_string(request.capacityChannels);
        if (expected.found) {
            EXPECT_TRUE(result.ok()) << context << ": the oracle found cost " << expected.cost << " but establish said " << (result.ok() ? "" : result.error().describe());
            if (!result.ok()) return false;
            const Connection& c = result.value();
            EXPECT_NEAR(c.path().cost(), expected.cost, 1e-9) << context;
            EXPECT_EQ(c.path().hopCount(), expected.hops) << context;
            const PathExpectation e{request.source, request.destination, request.constraints.metric, &request.constraints, &failures};
            EXPECT_VALID(checkPath(t, c.path(), e));
            // Routing -> ConnectionManager: the exact path that was returned is the one registered and allocated.
            const Connection* registered = manager.find(request.id);
            EXPECT_NE(registered, nullptr) << context;
            if (registered) {
                EXPECT_EQ(registered->path(), c.path()) << context;
            }
            EXPECT_TRUE(ledger.allocate(c.allocatedLinks(), request.capacityChannels)) << context;
            return true;
        }
        EXPECT_FALSE(result.ok()) << context << ": establish succeeded but the oracle found no feasible path";
        if (result.ok()) return false;
        // Error classification: capacity is blamed only if the route exists once capacity is ignored.
        RoutingConstraints ignoreCapacity = request.constraints;
        const bool routeWithoutCapacity = exhaustiveRoute(t, request.source, request.destination, ignoreCapacity, &failures).found;
        const ErrorCode want = routeWithoutCapacity ? ErrorCode::InsufficientCapacity
                               : reachableByAnyPath(t, request.source, request.destination) ? ErrorCode::NoFeasibleRoute
                                                                                            : ErrorCode::NoRoute;
        EXPECT_EQ(result.error().code, want) << context << ": " << result.error().describe();
        ++(want == ErrorCode::InsufficientCapacity ? counts.insufficient : want == ErrorCode::NoFeasibleRoute ? counts.noFeasible : counts.noRoute);
        (void)isRecovery;
        return false;
    };

    for (int op = 0; op < 120; ++op) {
        SCOPED_TRACE("op " + std::to_string(op));
        const std::uint32_t kind = rng.uniformInt(0, 9);
        if (kind <= 4 || active.empty()) {  // establish
            const std::size_t si = rng.uniformIndex(nodes.size());
            std::size_t di = rng.uniformIndex(nodes.size() - 1);
            if (di >= si) ++di;
            ConnectionRequest request{ConnectionId{nextId++}, nodes[si], nodes[di], rng.uniformInt(1, 3), {}};
            if (rng.uniformInt(0, 2) == 0) request.constraints = generateRandomConstraints(t, rng, 0.5);
            if (establish(request, false)) {
                active.push_back(request.id);
                ++counts.accepted;
            } else {
                ++counts.rejected;
            }
        } else if (kind <= 6) {  // release
            const std::size_t i = rng.uniformIndex(active.size());
            const Connection snapshot = *manager.find(active[i]);
            ASSERT_OK(manager.release(active[i]));
            ASSERT_TRUE(ledger.release(snapshot.allocatedLinks(), snapshot.capacityChannels()));
            active.erase(active.begin() + static_cast<long>(i));
            ++counts.released;
        } else if (kind == 7 && failures.anyFailed()) {  // recover something
            if (!failures.failedLinks().empty()) ASSERT_OK(failures.recoverLink(*failures.failedLinks().begin()));
            else ASSERT_OK(failures.recoverNode(*failures.failedNodes().begin()));
        } else {  // fail a link (or a node) and recover the affected connections
            Result<void> applied = rng.uniformInt(0, 4) == 0 ? failures.failNode(nodes[rng.uniformIndex(nodes.size())])
                                                              : failures.failLink(links[rng.uniformIndex(links.size())]);
            if (!applied.ok()) continue;  // already failed: a legitimate InvalidTransition, nothing changed
            ++counts.failuresApplied;
            std::vector<Connection> victims;
            for (const auto& [id, c] : manager.activeConnections())
                if (failures.isPathAffected(c.path())) victims.push_back(c);
            for (const Connection& v : victims) {
                ++counts.affected;
                ASSERT_TRUE(manager.detach(v.id()).ok());
                ASSERT_TRUE(ledger.release(v.allocatedLinks(), v.capacityChannels()));
                active.erase(std::find(active.begin(), active.end(), v.id()));
            }
            for (const Connection& v : victims) {
                if (establish(v.request(), true)) {
                    ++counts.rerouted;
                    active.push_back(v.id());
                } else {
                    ++counts.lost;
                }
            }
        }
        ASSERT_VALID(checkConnections(manager, t, &failures));
        ASSERT_VALID(checkResources(manager.resources(), t));
        ASSERT_VALID(checkFailureState(failures, t));
        for (const LinkId id : links) ASSERT_EQ(manager.resources().allocated(id), ledger.allocated(id)) << "link " << id.value();
        if (::testing::Test::HasFailure()) return;
    }
}

}  // namespace

TEST(ValConnections, RandomOperationsKeepEveryLayerConsistentAndMatchTheOracle) {
    Counts counts;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        drive(seed, counts);
        if (::testing::Test::HasFailure()) return;
    }
    EXPECT_GT(counts.accepted, 500);
    EXPECT_GT(counts.rejected, 200);
    EXPECT_GT(counts.released, 200);
    EXPECT_GT(counts.failuresApplied, 100);
    EXPECT_GT(counts.affected, 50);
    EXPECT_GT(counts.rerouted, 20);
    EXPECT_GT(counts.lost, 10);
    EXPECT_GT(counts.insufficient, 20) << "capacity-caused rejections must be exercised";
    EXPECT_GT(counts.noFeasible, 20);
}

TEST(ValConnections, ConnectionIdsAreUniqueAmongActiveConnections) {
    const Topology t = fixture("link_failure");
    ConnectionManager m{t};
    ASSERT_OK(m.establish(ConnectionRequest{ConnectionId{7}, NodeId{1}, NodeId{3}, 1, {}}));
    ASSERT_ERROR(m.establish(ConnectionRequest{ConnectionId{7}, NodeId{1}, NodeId{2}, 1, {}}), ErrorCode::DuplicateId);
    ASSERT_ERROR(m.establishOnPath(ConnectionRequest{ConnectionId{7}, NodeId{1}, NodeId{2}, 1, {}},
                                   must(Path::create(nodeIds({1, 2}), linkIds({1}), 1.0, CostMetric::Administrative))),
                 ErrorCode::DuplicateId);
    EXPECT_EQ(m.activeCount(), 1u);
    EXPECT_VALID(checkConnections(m, t));
    ASSERT_OK(m.release(ConnectionId{7}));
    ASSERT_OK(m.establish(ConnectionRequest{ConnectionId{7}, NodeId{1}, NodeId{2}, 1, {}})) << "a released id may be reused";
}

TEST(ValConnections, AllocationEqualsTheSumOfDemandsOnEveryLink) {
    // Congestion fixture: three 3-channel requests share the 4-channel top route and the 10-channel bottom route.
    const Topology t = fixture("capacity_congestion");
    ConnectionManager m{t};
    for (std::uint32_t id = 1; id <= 3; ++id) ASSERT_OK(m.establish(ConnectionRequest{ConnectionId{id}, NodeId{1}, NodeId{4}, 3, {}}));
    EXPECT_VALID(checkConnections(m, t));
    EXPECT_VALID(checkResources(m.resources(), t));
    EXPECT_EQ(m.resources().allocated(LinkId{1}), 3u) << "only the first request fits the 4-channel link";
    EXPECT_EQ(m.resources().allocated(LinkId{3}), 6u);
}

TEST(ValConnections, LostConnectionsHoldNoResources) {
    const Topology t = fixture("simple_line");
    FailureState failures(t);
    ConnectionManager m{t};
    m.setAvailability([&failures](const FiberLink& l) { return failures.isLinkUsable(l); });
    ASSERT_OK(m.establish(ConnectionRequest{ConnectionId{1}, NodeId{1}, NodeId{4}, 5, {}}));
    ASSERT_OK(failures.failLink(LinkId{2}));
    const Connection old = must(m.detach(ConnectionId{1}));
    ASSERT_FALSE(m.establish(old.request()).ok()) << "the line has no alternative";
    EXPECT_FALSE(m.contains(ConnectionId{1})) << "a lost connection is not active";
    EXPECT_EQ(m.resources().totalAllocated(), 0u);
    EXPECT_VALID(checkConnections(m, t, &failures));
    EXPECT_VALID(checkResources(m.resources(), t));
}

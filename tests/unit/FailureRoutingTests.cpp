#include <gtest/gtest.h>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/failures/FailureState.hpp"
#include "opticalnet/resources/ConnectionManager.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

constexpr NodeId A{1}, B{2}, C{3}, D{4};

//   A --l1(1)-- B --l2(1)-- C        primary A-C: A-B-C (cost 2)
//    \                     /
//     l3(2)-- D --l4(2)---          backup  A-C: A-D-C (cost 4)
Topology square(std::uint32_t channels = 10) {
    return makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, channels),
                                       routedLink(2, 2, 3, 1, 1, LinkDirection::Bidirectional, channels),
                                       routedLink(3, 1, 4, 1, 2, LinkDirection::Bidirectional, channels),
                                       routedLink(4, 4, 3, 1, 2, LinkDirection::Bidirectional, channels)});
}

ConnectionRequest req(std::uint32_t id, NodeId from, NodeId to, std::uint32_t channels, RoutingConstraints c = {}) {
    return ConnectionRequest{ConnectionId{id}, from, to, channels, std::move(c)};
}

// A manager whose routing consults a FailureState, exactly as the simulation engine wires it.
struct Fixture {
    explicit Fixture(const Topology& t) : failures(t), manager(t) {
        manager.setAvailability([this](const FiberLink& l) { return failures.isLinkUsable(l); });
    }
    FailureState failures;
    ConnectionManager manager;
};

}  // namespace

// ================================================================== routing sees the overlay

TEST(FailureRouting, HealthyNetworkUsesTheShortestRoute) {
    const Topology t = square();
    Fixture f(t);
    EXPECT_EQ(must(f.manager.establish(req(1, A, C, 2))).path().nodes(), nodeIds({1, 2, 3}));
}

TEST(FailureRouting, FailedLinkIsExcludedAndTheAlternateRouteIsUsed) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    const Connection c = must(f.manager.establish(req(1, A, C, 2)));
    EXPECT_EQ(c.path().nodes(), nodeIds({1, 4, 3}));
    EXPECT_EQ(c.allocatedLinks(), linkIds({3, 4}));
}

TEST(FailureRouting, FailedNodeIsNotEnteredLeftOrCrossed) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.failures.failNode(NodeId{2}));
    EXPECT_EQ(must(f.manager.establish(req(1, A, C, 1))).path().nodes(), nodeIds({1, 4, 3})) << "cannot pass through B";
    ASSERT_ERROR(f.manager.establish(req(2, A, B, 1)), ErrorCode::NoFeasibleRoute) << "cannot enter B";
    ASSERT_ERROR(f.manager.establish(req(3, B, C, 1)), ErrorCode::NoFeasibleRoute) << "cannot leave B";
}

TEST(FailureRouting, RecoveryRestoresTheShortestRoute) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.failures.failLink(LinkId{1}));
    EXPECT_EQ(must(f.manager.establish(req(1, A, C, 1))).path().nodes(), nodeIds({1, 4, 3}));
    ASSERT_OK(f.manager.release(ConnectionId{1}));
    ASSERT_OK(f.failures.recoverLink(LinkId{1}));
    EXPECT_EQ(must(f.manager.establish(req(2, A, C, 1))).path().nodes(), nodeIds({1, 2, 3}));
}

TEST(FailureRouting, NodeRecoveryRestoresRouting) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.failures.failNode(NodeId{2}));
    ASSERT_ERROR(f.manager.establish(req(1, A, B, 1)), ErrorCode::NoFeasibleRoute);
    ASSERT_OK(f.failures.recoverNode(NodeId{2}));
    EXPECT_EQ(must(f.manager.establish(req(1, A, B, 1))).path().nodes(), nodeIds({1, 2}));
}

TEST(FailureRouting, NoAlternateRouteIsNoFeasibleRouteNotInsufficientCapacity) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1)});
    Fixture f(t);
    ASSERT_OK(f.failures.failLink(LinkId{1}));
    ASSERT_ERROR(f.manager.establish(req(1, A, B, 1)), ErrorCode::NoFeasibleRoute) << "the cause is the failure, not capacity";
}

TEST(FailureRouting, FailureDoesNotChangeConfiguredCapacity) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    EXPECT_EQ(must(f.manager.resources().usage(LinkId{2})).total, 10u);
    EXPECT_EQ(must(f.manager.resources().usage(LinkId{2})).available, 10u);
    EXPECT_EQ(f.manager.resources().totalCapacity(), 40u);
}

TEST(FailureRouting, CapacityAndFailureCombine) {
    const Topology t = square(4);
    Fixture f(t);
    ASSERT_OK(f.manager.establish(req(1, A, D, 4)));  // fills link 3 (A-D)
    ASSERT_OK(f.failures.failLink(LinkId{2}));        // B-C down: only A-D-C remains, and A-D is full
    ASSERT_ERROR(f.manager.establish(req(2, A, C, 1)), ErrorCode::InsufficientCapacity)
        << "a route exists around the failure, but it has no free channel";
}

TEST(FailureRouting, CallerFilterStillAppliesAlongsideTheOverlay) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.failures.failLink(LinkId{3}));
    RoutingConstraints c;
    c.linkFilter = [](const FiberLink& l) { return l.id() != LinkId{1}; };  // caller bans l1 as well
    ASSERT_ERROR(f.manager.establish(req(1, A, C, 1, c)), ErrorCode::NoFeasibleRoute);
}

// ================================================================== explicit paths over failures

TEST(FailureRouting, EstablishOnPathRejectsFailedLinkAndFailedNode) {
    const Topology t = square();
    Fixture f(t);
    const Path primary = must(Path::create(nodeIds({1, 2, 3}), linkIds({1, 2}), 2.0, CostMetric::Administrative));
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    ASSERT_ERROR(f.manager.establishOnPath(req(1, A, C, 1), primary), ErrorCode::ResourceUnavailable);
    ASSERT_OK(f.failures.recoverLink(LinkId{2}));
    ASSERT_OK(f.failures.failNode(NodeId{2}));
    ASSERT_ERROR(f.manager.establishOnPath(req(1, A, C, 1), primary), ErrorCode::ResourceUnavailable);
    EXPECT_EQ(f.manager.resources().totalAllocated(), 0u) << "nothing allocated by the rejected attempts";
    ASSERT_OK(f.failures.recoverNode(NodeId{2}));
    ASSERT_OK(f.manager.establishOnPath(req(1, A, C, 1), primary));
}

TEST(FailureRouting, WithoutAnAvailabilityPredicateEverythingIsAvailable) {
    const Topology t = square();
    ConnectionManager m(t);
    EXPECT_EQ(must(m.establish(req(1, A, C, 1))).path().nodes(), nodeIds({1, 2, 3}));
}

// ================================================================== detach / re-establish (the recovery primitive)

TEST(FailureRecovery, DetachReleasesExactlyTheConnectionsResources) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.manager.establish(req(1, A, C, 3)));
    ASSERT_OK(f.manager.establish(req(2, A, B, 2)));
    const Connection gone = must(f.manager.detach(ConnectionId{1}));
    EXPECT_EQ(gone.id(), ConnectionId{1});
    EXPECT_EQ(gone.capacityChannels(), 3u);
    EXPECT_EQ(gone.allocatedLinks(), linkIds({1, 2}));
    EXPECT_FALSE(f.manager.contains(ConnectionId{1}));
    EXPECT_EQ(f.manager.resources().allocated(LinkId{1}), 2u) << "only connection 2 still holds link 1";
    EXPECT_EQ(f.manager.resources().allocated(LinkId{2}), 0u);
    EXPECT_TRUE(f.manager.validate().clean());
}

TEST(FailureRecovery, DetachOfUnknownConnectionChangesNothing) {
    const Topology t = square();
    Fixture f(t);
    ASSERT_OK(f.manager.establish(req(1, A, C, 3)));
    ASSERT_ERROR(f.manager.detach(ConnectionId{9}), ErrorCode::NotFound);
    EXPECT_EQ(f.manager.resources().totalAllocated(), 6u);
    ASSERT_OK(f.manager.detach(ConnectionId{1}));
    ASSERT_ERROR(f.manager.detach(ConnectionId{1}), ErrorCode::NotFound) << "no double release";
    EXPECT_EQ(f.manager.resources().totalAllocated(), 0u);
}

// Fixture A at the manager level: the connection keeps its id and demand and moves to A -> D -> C.
TEST(FailureRecovery, FixtureA_LinkFailureReroutesOverTheAlternatePathKeepingTheId) {
    const Topology t = square();
    Fixture f(t);
    const Connection original = must(f.manager.establish(req(42, A, C, 4)));
    ASSERT_EQ(original.path().nodes(), nodeIds({1, 2, 3}));

    ASSERT_OK(f.failures.failLink(LinkId{2}));                                // B-C fails
    ASSERT_TRUE(f.failures.isPathAffected(f.manager.find(ConnectionId{42})->path()));  // detected
    const Connection old = must(f.manager.detach(ConnectionId{42}));
    const Connection rerouted = must(f.manager.establish(old.request()));

    EXPECT_EQ(rerouted.id(), ConnectionId{42}) << "same ConnectionId";
    EXPECT_EQ(rerouted.path().nodes(), nodeIds({1, 4, 3}));
    EXPECT_EQ(rerouted.capacityChannels(), 4u) << "same demand";
    EXPECT_EQ(f.manager.resources().allocated(LinkId{1}), 0u) << "old links released";
    EXPECT_EQ(f.manager.resources().allocated(LinkId{2}), 0u);
    EXPECT_EQ(f.manager.resources().allocated(LinkId{3}), 4u);
    EXPECT_EQ(f.manager.resources().allocated(LinkId{4}), 4u);
    EXPECT_EQ(f.manager.activeCount(), 1u);
    EXPECT_FALSE(f.failures.isPathAffected(rerouted.path()));
    EXPECT_TRUE(f.manager.validate().clean());
}

// Fixture B: the only route is cut -> the connection is gone and nothing leaks.
TEST(FailureRecovery, FixtureB_NoAlternateRouteLeavesAConsistentLostState) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1)});
    Fixture f(t);
    ASSERT_OK(f.manager.establish(req(1, A, C, 5)));
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    const Connection old = must(f.manager.detach(ConnectionId{1}));
    ASSERT_ERROR(f.manager.establish(old.request()), ErrorCode::NoFeasibleRoute);
    EXPECT_EQ(f.manager.activeCount(), 0u);
    EXPECT_EQ(f.manager.resources().totalAllocated(), 0u) << "no leaked channels on the healthy link either";
    EXPECT_TRUE(f.manager.validate().clean());
}

// Fixture E: the alternate path exists structurally but is full -> clean failure, no partial allocation.
TEST(FailureRecovery, FixtureE_FullAlternatePathMeansNoPartialAllocation) {
    const Topology t = square(10);
    Fixture f(t);
    ASSERT_OK(f.manager.establish(req(1, A, C, 4)));    // A-B-C
    ASSERT_OK(f.manager.establish(req(2, D, C, 10)));   // fills link 4 (D-C)
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    const Connection old = must(f.manager.detach(ConnectionId{1}));
    ASSERT_ERROR(f.manager.establish(old.request()), ErrorCode::InsufficientCapacity);
    EXPECT_EQ(f.manager.resources().allocated(LinkId{3}), 0u) << "A-D must not hold a partial allocation";
    EXPECT_EQ(f.manager.resources().allocated(LinkId{4}), 10u) << "the unrelated connection is intact";
    EXPECT_EQ(f.manager.resources().allocated(LinkId{1}), 0u);
    EXPECT_EQ(f.manager.activeCount(), 1u);
    EXPECT_TRUE(f.manager.validate().clean());
}

TEST(FailureRecovery, RerouteReusesTheOriginalConstraints) {
    const Topology t = makeTopology({1, 2, 3, 4, 5}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1), routedLink(3, 1, 4, 1, 1),
                                                       routedLink(4, 4, 5, 1, 1), routedLink(5, 5, 3, 1, 1)});
    Fixture f(t);
    RoutingConstraints limited;
    limited.maxHops = 2;  // the 3-hop detour A-D-E-C is forbidden for this connection
    ASSERT_OK(f.manager.establish(req(1, A, C, 1, limited)));
    ASSERT_OK(f.manager.establish(req(2, A, C, 1)));  // unconstrained twin
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    const Connection c1 = must(f.manager.detach(ConnectionId{1}));
    const Connection c2 = must(f.manager.detach(ConnectionId{2}));
    ASSERT_ERROR(f.manager.establish(c1.request()), ErrorCode::NoFeasibleRoute) << "maxHops = 2 is not relaxed by recovery";
    EXPECT_EQ(must(f.manager.establish(c2.request())).path().hopCount(), 3u);
}

TEST(FailureRecovery, RerouteReusesTheBlockedNodeConstraint) {
    const Topology t = square();
    Fixture f(t);
    RoutingConstraints c;
    c.blockedNodes.insert(D);  // never via D
    ASSERT_OK(f.manager.establish(req(1, A, C, 1, c)));
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    const Connection old = must(f.manager.detach(ConnectionId{1}));
    ASSERT_ERROR(f.manager.establish(old.request()), ErrorCode::NoFeasibleRoute) << "the only other route goes through blocked node D";
}

TEST(FailureRecovery, DetachFreesOnlyItsOwnChannelsBeforeReestablishing) {
    // Detaching frees exactly the detached connection's channels: afterwards link 1 holds only the other
    // connection's 4, and the detached connection can be re-established over the detour.
    const Topology t = square(10);
    Fixture f(t);
    ASSERT_OK(f.manager.establish(req(1, A, C, 6)));  // A-B-C
    ASSERT_OK(f.manager.establish(req(2, A, B, 4)));  // l1 now has 10
    ASSERT_OK(f.failures.failLink(LinkId{2}));
    const Connection a = must(f.manager.detach(ConnectionId{1}));
    EXPECT_EQ(f.manager.resources().allocated(LinkId{1}), 4u);
    const Connection again = must(f.manager.establish(a.request()));
    EXPECT_EQ(again.path().nodes(), nodeIds({1, 4, 3}));
    EXPECT_TRUE(f.manager.validate().clean());
}

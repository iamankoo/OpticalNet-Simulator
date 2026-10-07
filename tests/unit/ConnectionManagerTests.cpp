#include <gtest/gtest.h>

#include <random>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/resources/ConnectionManager.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

ConnectionRequest req(std::uint32_t id, std::uint32_t from, std::uint32_t to, std::uint32_t channels,
                      RoutingConstraints constraints = {}) {
    return ConnectionRequest{ConnectionId{id}, NodeId{from}, NodeId{to}, channels, std::move(constraints)};
}

std::uint32_t used(const ConnectionManager& m, std::uint32_t link) { return m.resources().allocated(LinkId{link}); }

// A --l1(cost 2, cap 100)-- B --l2(cost 2, cap 100)-- D        shortest A-D: via B (cost 4)
// A --l3(cost 5, cap 100)-- C --l4(cost 1, cap 100)-- D        alternative:  via C (cost 6)
Topology diamond() {
    return makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 2, LinkDirection::Bidirectional, 100),
                                       routedLink(2, 2, 4, 1, 2, LinkDirection::Bidirectional, 100),
                                       routedLink(3, 1, 3, 1, 5, LinkDirection::Bidirectional, 100),
                                       routedLink(4, 3, 4, 1, 1, LinkDirection::Bidirectional, 100)});
}

}  // namespace

// ------------------------------------------------------------------ request

TEST(ConnectionRequest, ValidationRules) {
    EXPECT_TRUE(req(1, 1, 2, 5).validate().ok());
    ASSERT_ERROR(req(1, 1, 2, 0).validate(), ErrorCode::InvalidArgument);
    ASSERT_ERROR(req(1, 3, 3, 5).validate(), ErrorCode::InvalidArgument);
}

TEST(ConnectionRequest, NegativeCapacityCannotBeExpressed) {
    // Capacity is unsigned; a negative number cannot reach the manager as a valid demand.
    static_assert(std::is_unsigned_v<decltype(ConnectionRequest::capacityChannels)>);
    SUCCEED();
}

// ---------------------------------------------------------------- establish

TEST(ConnectionManager, EstablishAllocatesTheRoutedPath) {
    const Topology t = diamond();
    ConnectionManager m{t};
    const Connection c = must(m.establish(req(7, 1, 4, 30)));

    EXPECT_EQ(c.id(), ConnectionId{7});
    EXPECT_EQ(c.request().source, NodeId{1});
    EXPECT_EQ(c.request().destination, NodeId{4});
    EXPECT_EQ(c.capacityChannels(), 30u);
    EXPECT_EQ(c.path().nodes(), nodeIds({1, 2, 4}));
    EXPECT_EQ(c.allocatedLinks(), linkIds({1, 2}));
    EXPECT_EQ(used(m, 1), 30u);
    EXPECT_EQ(used(m, 2), 30u);
    EXPECT_EQ(used(m, 3), 0u);
    EXPECT_EQ(m.resources().totalAllocated(), 60u);
    EXPECT_TRUE(m.contains(ConnectionId{7}));
    EXPECT_EQ(m.activeCount(), 1u);
    EXPECT_TRUE(m.validate().clean());
}

TEST(ConnectionManager, ConstraintsFromTheRequestAreHonoured) {
    const Topology t = diamond();
    ConnectionManager m{t};
    RoutingConstraints c;
    c.blockedNodes.insert(NodeId{2});
    const Connection conn = must(m.establish(req(1, 1, 4, 10, c)));
    EXPECT_EQ(conn.path().nodes(), nodeIds({1, 3, 4}));
}

// -------------------------------------------------------------- capacity cases

TEST(ConnectionManager, ThreeConnectionsFillTheLinkAndTheFourthIsRejected) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 100)});
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 1, 2, 30)));
    ASSERT_OK(m.establish(req(2, 1, 2, 40)));
    ASSERT_OK(m.establish(req(3, 1, 2, 30)));
    EXPECT_EQ(m.resources().totalAllocated(), 100u);
    EXPECT_EQ(m.resources().totalAvailable(), 0u);
    ASSERT_ERROR(m.establish(req(4, 1, 2, 1)), ErrorCode::InsufficientCapacity);
    EXPECT_EQ(m.activeCount(), 3u);
    EXPECT_FALSE(m.contains(ConnectionId{4}));
    EXPECT_EQ(m.resources().totalAllocated(), 100u);
    EXPECT_TRUE(m.validate().clean());
}

TEST(ConnectionManager, InsufficientCapacityOnTheOnlyRouteChangesNothing) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 100),
                                                routedLink(2, 2, 3, 1, 1, LinkDirection::Bidirectional, 10)});
    ConnectionManager m{t};
    ASSERT_ERROR(m.establish(req(1, 1, 3, 20)), ErrorCode::InsufficientCapacity) << "the final link is too small";
    EXPECT_EQ(m.resources().totalAllocated(), 0u) << "the first link must not stay allocated";
    EXPECT_EQ(m.activeCount(), 0u);
    ASSERT_OK(m.establish(req(1, 1, 3, 10))) << "the same id can be used again after a failure";
}

TEST(ConnectionManager, NoRouteIsDistinctFromInsufficientCapacity) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1)});
    ConnectionManager m{t};
    ASSERT_ERROR(m.establish(req(1, 1, 3, 1)), ErrorCode::NoRoute);
}

TEST(ConnectionManager, ConstraintInfeasibilityIsNotReportedAsCapacity) {
    const Topology t = diamond();
    ConnectionManager m{t};
    RoutingConstraints c;
    c.maxCost = 1.0;  // no route is that cheap, whatever the capacity
    ASSERT_ERROR(m.establish(req(1, 1, 4, 5, c)), ErrorCode::NoFeasibleRoute);
}

// --------------------------------------------------- routing/resource integration

TEST(ConnectionManagerRouting, ShortestRouteLackingCapacityIsAvoided) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 2, 4, 80)));  // uses link 2 (B-D): 20 channels left

    const Connection c = must(m.establish(req(2, 1, 4, 50)));  // shortest A-B-D lacks capacity on B-D
    EXPECT_EQ(c.path().nodes(), nodeIds({1, 3, 4})) << "the alternative route A-C-D must be chosen";
    EXPECT_EQ(c.allocatedLinks(), linkIds({3, 4}));
    EXPECT_EQ(used(m, 1), 0u);
    EXPECT_EQ(used(m, 2), 80u);
    EXPECT_EQ(used(m, 3), 50u);
    EXPECT_EQ(used(m, 4), 50u);
    EXPECT_TRUE(m.validate().clean());
}

TEST(ConnectionManagerRouting, ShortestRouteReturnsOnceCapacityIsReleased) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 2, 4, 80)));
    EXPECT_EQ(must(m.establish(req(2, 1, 4, 50))).path().nodes(), nodeIds({1, 3, 4}));
    ASSERT_OK(m.release(ConnectionId{2}));
    ASSERT_OK(m.release(ConnectionId{1}));
    EXPECT_EQ(must(m.establish(req(3, 1, 4, 50))).path().nodes(), nodeIds({1, 2, 4}));
}

TEST(ConnectionManagerRouting, WhenEveryRouteIsFullTheRequestIsRejected) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 2, 4, 90)));
    ASSERT_OK(m.establish(req(2, 3, 4, 90)));
    ASSERT_ERROR(m.establish(req(3, 1, 4, 50)), ErrorCode::InsufficientCapacity);
    ASSERT_OK(m.establish(req(4, 1, 4, 10))) << "a small request still fits on either route";
}

TEST(ConnectionManagerRouting, CallerFilterIsCombinedWithTheCapacityFilter) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 3, 4, 95)));  // link 4 nearly full
    RoutingConstraints c;
    c.linkFilter = [](const FiberLink& l) { return l.id() != LinkId{1}; };  // caller bans link 1 (A-B)
    // Via B is banned by the caller, via C lacks capacity on link 4: nothing is left.
    ASSERT_ERROR(m.establish(req(2, 1, 4, 10, c)), ErrorCode::InsufficientCapacity);
    c.linkFilter = [](const FiberLink&) { return true; };
    EXPECT_EQ(must(m.establish(req(3, 1, 4, 10, c))).path().nodes(), nodeIds({1, 2, 4}));
}

TEST(ConnectionManagerRouting, RoutingEngineHoldsNoResourceState) {
    // The same Topology and engine, two managers: they do not influence each other.
    const Topology t = diamond();
    ConnectionManager first{t};
    ConnectionManager second{t};
    ASSERT_OK(first.establish(req(1, 2, 4, 100)));
    EXPECT_EQ(must(second.establish(req(1, 1, 4, 10))).path().nodes(), nodeIds({1, 2, 4}));
    EXPECT_EQ(must(first.establish(req(2, 1, 4, 10))).path().nodes(), nodeIds({1, 3, 4}));
}

// ------------------------------------------------------------- parallel links

TEST(ConnectionManagerParallel, TheExactChosenLinkIsAllocated) {
    // Two A-B fibers, cheaper link 1 and dearer link 2, 40 channels each.
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 40),
                                             routedLink(2, 1, 2, 1, 2, LinkDirection::Bidirectional, 40)});
    ConnectionManager m{t};
    const Connection first = must(m.establish(req(1, 1, 2, 30)));
    EXPECT_EQ(first.allocatedLinks(), linkIds({1}));
    EXPECT_EQ(used(m, 1), 30u);
    EXPECT_EQ(used(m, 2), 0u);

    const Connection second = must(m.establish(req(2, 1, 2, 30)));  // link 1 has only 10 free
    EXPECT_EQ(second.allocatedLinks(), linkIds({2}));
    EXPECT_EQ(used(m, 1), 30u);
    EXPECT_EQ(used(m, 2), 30u);

    ASSERT_OK(m.release(ConnectionId{2}));
    EXPECT_EQ(used(m, 1), 30u) << "releasing connection 2 must not touch the parallel link 1";
    EXPECT_EQ(used(m, 2), 0u);
    ASSERT_ERROR(m.establish(req(3, 1, 2, 50)), ErrorCode::InsufficientCapacity);
}

// -------------------------------------------------------------------- release

TEST(ConnectionManagerRelease, RestoresExactlyWhatWasAllocated) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 1, 4, 30)));
    ASSERT_OK(m.establish(req(2, 1, 4, 20)));
    const auto before = m.resources().allocations();
    ASSERT_OK(m.establish(req(3, 1, 4, 25)));
    ASSERT_OK(m.release(ConnectionId{3}));
    EXPECT_EQ(m.resources().allocations(), before);
    EXPECT_FALSE(m.contains(ConnectionId{3}));
    EXPECT_EQ(m.find(ConnectionId{3}), nullptr);
    EXPECT_EQ(m.activeCount(), 2u);
    EXPECT_TRUE(m.validate().clean());
}

TEST(ConnectionManagerRelease, AllocateReleaseAllocateAgain) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 10)});
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 1, 2, 10)));
    ASSERT_ERROR(m.establish(req(2, 1, 2, 10)), ErrorCode::InsufficientCapacity);
    ASSERT_OK(m.release(ConnectionId{1}));
    ASSERT_OK(m.establish(req(2, 1, 2, 10)));
    ASSERT_OK(m.release(ConnectionId{2}));
    ASSERT_OK(m.establish(req(1, 1, 2, 10))) << "released ids can be reused";
    EXPECT_EQ(m.resources().totalAllocated(), 10u);
}

TEST(ConnectionManagerRelease, UnknownAndAlreadyReleasedConnectionsAreRejectedSafely) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_ERROR(m.release(ConnectionId{42}), ErrorCode::NotFound);
    ASSERT_OK(m.establish(req(1, 1, 4, 10)));
    ASSERT_OK(m.release(ConnectionId{1}));
    ASSERT_ERROR(m.release(ConnectionId{1}), ErrorCode::NotFound);
    EXPECT_EQ(m.resources().totalAllocated(), 0u) << "a second release must not release anything twice";
    EXPECT_TRUE(m.validate().clean());
}

TEST(ConnectionManagerRelease, ReleasesTheStoredLinksNotARecomputedRoute) {
    Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 5, LinkDirection::Bidirectional, 40)});
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 1, 2, 30)));
    // The topology changes: a cheaper parallel link appears, so a fresh route would now use link 2.
    ASSERT_OK(t.addLink(routedLink(2, 1, 2, 1, 1, LinkDirection::Bidirectional, 40)));
    ASSERT_OK(m.release(ConnectionId{1}));
    EXPECT_EQ(used(m, 1), 0u) << "link 1 was the allocated one";
    EXPECT_EQ(used(m, 2), 0u);
    EXPECT_TRUE(m.validate().clean());
}

TEST(ConnectionManagerRelease, ReleaseSucceedsEvenIfTheLinkWasRemovedFromTheTopology) {
    Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 1, 4, 10)));
    ASSERT_OK(t.removeLink(LinkId{2}));
    EXPECT_FALSE(m.validate().valid()) << "allocation on a vanished link is reported";
    ASSERT_OK(m.release(ConnectionId{1}));
    EXPECT_TRUE(m.validate().clean());
}

// -------------------------------------------------------------- invalid input

TEST(ConnectionManagerInvalid, DuplicateConnectionIdIsRejectedWithoutSideEffects) {
    const Topology t = diamond();
    ConnectionManager m{t};
    const Connection original = must(m.establish(req(1, 1, 4, 10)));
    ASSERT_ERROR(m.establish(req(1, 1, 4, 10)), ErrorCode::DuplicateId);
    ASSERT_ERROR(m.establish(req(1, 2, 3, 5)), ErrorCode::DuplicateId);
    EXPECT_EQ(m.activeCount(), 1u);
    EXPECT_EQ(m.resources().totalAllocated(), 20u);
    EXPECT_EQ(m.find(ConnectionId{1})->path(), original.path());
}

TEST(ConnectionManagerInvalid, BadRequestsAreRejected) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_ERROR(m.establish(req(1, 1, 4, 0)), ErrorCode::InvalidArgument) << "zero capacity";
    ASSERT_ERROR(m.establish(req(2, 1, 1, 5)), ErrorCode::InvalidArgument) << "source == destination";
    ASSERT_ERROR(m.establish(req(3, 99, 4, 5)), ErrorCode::NotFound) << "unknown source";
    ASSERT_ERROR(m.establish(req(4, 1, 99, 5)), ErrorCode::NotFound) << "unknown destination";
    ASSERT_ERROR(m.establish(req(5, 98, 99, 5)), ErrorCode::NotFound);
    EXPECT_EQ(m.activeCount(), 0u);
    EXPECT_EQ(m.resources().totalAllocated(), 0u);
}

TEST(ConnectionManagerInvalid, DemandAboveEveryLinksTotalCapacityIsInsufficient) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_ERROR(m.establish(req(1, 1, 4, 101)), ErrorCode::InsufficientCapacity);
    ASSERT_ERROR(m.establish(req(2, 1, 4, 4'000'000'000u)), ErrorCode::InsufficientCapacity);
}

// -------------------------------------------------------------- explicit paths

namespace {
Path pathOf(std::vector<NodeId> nodes, std::vector<LinkId> links) {
    return must(Path::create(std::move(nodes), std::move(links), static_cast<double>(links.size()), CostMetric::HopCount));
}
}  // namespace

TEST(ConnectionManagerPath, ValidExplicitPathIsAllocated) {
    const Topology t = diamond();
    ConnectionManager m{t};
    const Connection c = must(m.establishOnPath(req(1, 1, 4, 25), pathOf(nodeIds({1, 3, 4}), linkIds({3, 4}))));
    EXPECT_EQ(c.allocatedLinks(), linkIds({3, 4}));
    EXPECT_EQ(used(m, 3), 25u);
    EXPECT_EQ(used(m, 4), 25u);
    EXPECT_EQ(used(m, 1), 0u);
}

TEST(ConnectionManagerPath, InvalidPathsAreRejectedWithoutAllocating) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_ERROR(m.establishOnPath(req(1, 1, 4, 5), pathOf(nodeIds({1}), {})), ErrorCode::InvalidArgument) << "empty/trivial path";
    ASSERT_ERROR(m.establishOnPath(req(2, 1, 4, 5), pathOf(nodeIds({1, 2}), linkIds({1}))), ErrorCode::InvalidArgument)
        << "ends at the wrong node";
    ASSERT_ERROR(m.establishOnPath(req(3, 1, 4, 5), pathOf(nodeIds({2, 4}), linkIds({2}))), ErrorCode::InvalidArgument)
        << "starts at the wrong node";
    ASSERT_ERROR(m.establishOnPath(req(4, 1, 4, 5), pathOf(nodeIds({1, 2, 4}), linkIds({1, 99}))), ErrorCode::NotFound)
        << "unknown link";
    ASSERT_ERROR(m.establishOnPath(req(5, 1, 4, 5), pathOf(nodeIds({1, 2, 4}), linkIds({1, 4}))), ErrorCode::InvalidArgument)
        << "link 4 does not join nodes 2 and 4";
    ASSERT_ERROR(m.establishOnPath(req(6, 1, 4, 0), pathOf(nodeIds({1, 2, 4}), linkIds({1, 2}))), ErrorCode::InvalidArgument)
        << "zero capacity";
    EXPECT_EQ(m.resources().totalAllocated(), 0u);
    EXPECT_EQ(m.activeCount(), 0u);
}

TEST(ConnectionManagerPath, DirectedLinkCannotBeUsedBackwards) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Directed)});
    ConnectionManager m{t};
    ASSERT_ERROR(m.establishOnPath(req(1, 2, 1, 5), pathOf(nodeIds({2, 1}), linkIds({1}))), ErrorCode::InvalidArgument);
    ASSERT_OK(m.establishOnPath(req(2, 1, 2, 5), pathOf(nodeIds({1, 2}), linkIds({1}))));
}

TEST(ConnectionManagerPath, InsufficientCapacityOnAnExplicitPathAllocatesNothing) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 2, 4, 95)));  // link 2 almost full
    const auto before = m.resources().allocations();
    ASSERT_ERROR(m.establishOnPath(req(2, 1, 4, 10), pathOf(nodeIds({1, 2, 4}), linkIds({1, 2}))),
                 ErrorCode::InsufficientCapacity);
    EXPECT_EQ(m.resources().allocations(), before) << "link 1 must stay untouched";
    EXPECT_FALSE(m.contains(ConnectionId{2}));
}

TEST(ConnectionManagerPath, DuplicateIdOnExplicitPathIsRejected) {
    const Topology t = diamond();
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 1, 4, 5)));
    ASSERT_ERROR(m.establishOnPath(req(1, 1, 4, 5), pathOf(nodeIds({1, 3, 4}), linkIds({3, 4}))), ErrorCode::DuplicateId);
}

// ------------------------------------------------------------------- registry

TEST(ConnectionRegistry, InsertLookupReleaseAndOrdering) {
    const Topology t = diamond();
    ConnectionManager m{t};
    EXPECT_EQ(m.activeCount(), 0u);
    EXPECT_TRUE(m.activeConnections().empty());
    EXPECT_EQ(m.find(ConnectionId{1}), nullptr);
    EXPECT_FALSE(m.contains(ConnectionId{1}));

    for (std::uint32_t id : {5u, 2u, 9u}) ASSERT_OK(m.establish(req(id, 1, 4, 1)));
    EXPECT_EQ(m.activeCount(), 3u);
    std::vector<std::uint32_t> ids;
    for (const auto& [id, c] : m.activeConnections()) {
        ids.push_back(id.value());
        EXPECT_EQ(c.id(), id);
    }
    EXPECT_EQ(ids, (std::vector<std::uint32_t>{2, 5, 9}));
    ASSERT_NE(m.find(ConnectionId{5}), nullptr);
    EXPECT_EQ(m.find(ConnectionId{5})->capacityChannels(), 1u);

    ASSERT_OK(m.release(ConnectionId{5}));
    EXPECT_EQ(m.activeCount(), 2u);
    EXPECT_EQ(m.find(ConnectionId{5}), nullptr);
}

TEST(ConnectionRegistry, ConnectionsAreImmutableSnapshots) {
    const Topology t = diamond();
    ConnectionManager m{t};
    const Connection copy = must(m.establish(req(1, 1, 4, 10)));
    ASSERT_OK(m.release(ConnectionId{1}));
    EXPECT_EQ(copy.allocatedLinks(), linkIds({1, 2})) << "a returned Connection stays valid after release";
}

// ------------------------------------------------------------ accounting views

TEST(ConnectionManagerAccounting, InvariantsHoldAfterEveryOperation) {
    const Topology t = diamond();
    ConnectionManager m{t};
    const auto check = [&m] {
        for (const auto& [id, u] : m.resources().usages()) EXPECT_EQ(u.allocated + u.available, u.total) << id;
        EXPECT_EQ(m.resources().totalAllocated() + m.resources().totalAvailable(), m.resources().totalCapacity());
        EXPECT_TRUE(m.validate().clean());
    };
    check();
    ASSERT_OK(m.establish(req(1, 1, 4, 40)));
    check();
    ASSERT_OK(m.establish(req(2, 1, 4, 70)));
    check();
    EXPECT_FALSE(m.establish(req(3, 1, 4, 90)).ok());
    check();
    ASSERT_OK(m.release(ConnectionId{1}));
    check();
    EXPECT_FALSE(m.release(ConnectionId{1}).ok());
    check();
}

// ---------------------------------------------------------------------- stress

// 20x20 grid, 8 channels per link, thousands of random requests and releases. Resource accounting
// must always equal what the active connections hold, and the network must drain to zero.
TEST(ConnectionManagerStress, GridWithManyConnectionsStaysConsistent) {
    constexpr std::uint32_t kSide = 20;
    const auto node = [](std::uint32_t r, std::uint32_t c) { return r * kSide + c + 1; };
    Topology t;
    for (std::uint32_t i = 1; i <= kSide * kSide; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    std::uint32_t link = 1;
    for (std::uint32_t r = 0; r < kSide; ++r) {
        for (std::uint32_t c = 0; c < kSide; ++c) {
            if (c + 1 < kSide) ASSERT_OK(t.addLink(routedLink(link++, node(r, c), node(r, c + 1), 10, 1, LinkDirection::Bidirectional, 8)));
            if (r + 1 < kSide) ASSERT_OK(t.addLink(routedLink(link++, node(r, c), node(r + 1, c), 10, 1, LinkDirection::Bidirectional, 8)));
        }
    }

    ConnectionManager m{t};
    std::mt19937 rng(99);
    std::vector<std::uint32_t> active;
    std::uint32_t nextId = 1;
    int accepted = 0, rejected = 0;

    for (int op = 0; op < 4000; ++op) {
        if (active.empty() || rng() % 10 < 6) {
            const std::uint32_t s = rng() % (kSide * kSide) + 1;
            std::uint32_t d = rng() % (kSide * kSide) + 1;
            if (d == s) d = s % (kSide * kSide) + 1;
            const std::uint32_t id = nextId++;
            const auto result = m.establish(req(id, s, d, 1 + rng() % 3));
            if (result.ok()) {
                active.push_back(id);
                ++accepted;
            } else {
                EXPECT_EQ(result.error().code, ErrorCode::InsufficientCapacity) << result.error().describe();
                ++rejected;
            }
        } else {
            const std::size_t i = rng() % active.size();
            ASSERT_OK(m.release(ConnectionId{active[i]}));
            active.erase(active.begin() + static_cast<long>(i));
        }
        if (op % 400 == 0) ASSERT_TRUE(m.validate().clean()) << "op " << op;
    }

    std::uint64_t expected = 0;
    for (const auto& [id, c] : m.activeConnections()) expected += static_cast<std::uint64_t>(c.capacityChannels()) * c.path().hopCount();
    EXPECT_EQ(m.resources().totalAllocated(), expected);
    EXPECT_EQ(m.activeCount(), active.size());
    for (const auto& [id, u] : m.resources().usages()) {
        EXPECT_LE(u.allocated, u.total);
        EXPECT_EQ(u.allocated + u.available, u.total);
    }
    EXPECT_TRUE(m.validate().clean());
    EXPECT_GT(accepted, 1500);
    EXPECT_GT(rejected, 20) << "the grid must actually become congested";

    for (std::uint32_t id : active) ASSERT_OK(m.release(ConnectionId{id}));
    EXPECT_EQ(m.resources().totalAllocated(), 0u);
    EXPECT_TRUE(m.resources().allocations().empty());
    EXPECT_EQ(m.activeCount(), 0u);
    EXPECT_TRUE(m.validate().clean());
}

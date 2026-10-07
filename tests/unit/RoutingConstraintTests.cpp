#include <gtest/gtest.h>

#include <limits>
#include <map>

#include "RoutingTestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

constexpr NodeId A{1}, B{2}, C{3}, D{4};

RoutingConstraints admin() {
    RoutingConstraints c;
    c.metric = CostMetric::Administrative;
    return c;
}

// Chain 1-2-3-4-5 (admin cost 1 per link, 4 hops, cost 4) plus an expensive express 1-5 (cost 10, 1 hop).
Topology chainWithExpress() {
    return makeTopology({1, 2, 3, 4, 5}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1), routedLink(3, 3, 4, 1, 1),
                                          routedLink(4, 4, 5, 1, 1), routedLink(5, 1, 5, 1, 10)});
}

}  // namespace

// ----------------------------------------------------------------- max cost

TEST(RoutingMaxCost, GenerousLimitDoesNotChangeTheAnswer) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxCost = 1000.0;
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, NodeId{5}, c)), must(RoutingEngine{}.findPath(t, A, NodeId{5}, admin())));
}

TEST(RoutingMaxCost, LimitExactlyAtTheBoundaryIsFeasible) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxCost = 4.0;  // cheapest route costs exactly 4
    const Path p = must(RoutingEngine{}.findPath(t, A, NodeId{5}, c));
    EXPECT_DOUBLE_EQ(p.cost(), 4.0);
}

TEST(RoutingMaxCost, LimitJustBelowTheBoundaryIsInfeasible) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxCost = 3.999;
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, NodeId{5}, c), ErrorCode::NoFeasibleRoute)
        << "a route exists, only the constraint rules it out";
}

TEST(RoutingMaxCost, InfeasibleConstraintIsDistinctFromNoRoute) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 5)});
    auto c = admin();
    c.maxCost = 1.0;
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, C, c), ErrorCode::NoRoute) << "unreachable regardless of constraints";
}

TEST(RoutingMaxCost, ZeroLimitAllowsOnlyTheTrivialPath) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxCost = 0.0;
    EXPECT_TRUE(must(RoutingEngine{}.findPath(t, A, A, c)).isTrivial());
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingMaxCost, InvalidLimitsAreRejected) {
    const Topology t = chainWithExpress();
    for (double bad : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto c = admin();
        c.maxCost = bad;
        ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::InvalidArgument) << bad;
    }
}

TEST(RoutingMaxCost, OpticalReachIsADistanceLimit) {
    // 60 km via node 2, 160 km via node 3. A transceiver with 100 km reach can only use the first.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 30, 20), routedLink(2, 2, 4, 30, 20),
                                                    routedLink(3, 1, 3, 80, 1), routedLink(4, 3, 4, 80, 1)});
    const Transceiver trx = makeTransceiver(1, 100.0, 100.0);
    RoutingConstraints c;
    c.metric = CostMetric::Distance;
    c.maxCost = trx.reachKm();
    const Path p = must(RoutingEngine{}.findPath(t, A, D, c));
    EXPECT_TRUE(trx.canReach(p.cost()));

    const Transceiver shortReach = makeTransceiver(2, 100.0, 50.0);
    c.maxCost = shortReach.reachKm();
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, D, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingMaxCost, LimitIsInTheUnitsOfTheChosenMetric) {
    // Path 1-2 is 100 km but admin cost 1.
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 100, 1)});
    auto c = admin();
    c.maxCost = 1.0;
    EXPECT_TRUE(RoutingEngine{}.findPath(t, A, B, c).ok());
    c.metric = CostMetric::Distance;
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
    c.maxCost = 100.0;
    EXPECT_TRUE(RoutingEngine{}.findPath(t, A, B, c).ok());
}

TEST(RoutingMaxCost, PrunesExplorationInsteadOfSearchingEverything) {
    // 2000-node chain; with maxCost 5 only the first few nodes can ever be reached.
    constexpr std::uint32_t kNodes = 2000;
    Topology t;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    for (std::uint32_t i = 1; i < kNodes; ++i) ASSERT_OK(t.addLink(routedLink(i, i, i + 1, 1, 1)));

    std::size_t inspected = 0;
    auto c = admin();
    c.maxCost = 5.0;
    c.linkFilter = [&inspected](const FiberLink&) {
        ++inspected;
        return true;
    };
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, NodeId{kNodes}, c), ErrorCode::NoFeasibleRoute);
    EXPECT_LT(inspected, 40u) << "the search must stop extending paths once they exceed maxCost";
}

// ----------------------------------------------------------------- max hops

TEST(RoutingMaxHops, WithoutLimitTheCheapestPathWinsEvenIfLong) {
    const Topology t = chainWithExpress();
    const Path p = must(RoutingEngine{}.findPath(t, A, NodeId{5}, admin()));
    EXPECT_EQ(p.hopCount(), 4u);
    EXPECT_DOUBLE_EQ(p.cost(), 4.0);
}

TEST(RoutingMaxHops, BindingLimitFindsTheCostlierShortPath) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxHops = 1;
    const Path p = must(RoutingEngine{}.findPath(t, A, NodeId{5}, c));
    EXPECT_EQ(p.links(), linkIds({5})) << "the cheapest path has 4 hops, so the 1-hop express link must be used";
    EXPECT_DOUBLE_EQ(p.cost(), 10.0);
    EXPECT_EQ(p.hopCount(), 1u);
    expectValidPath(t, p, A, NodeId{5}, CostMetric::Administrative);
}

TEST(RoutingMaxHops, LimitExactlyAtTheBoundaryKeepsTheCheapestPath) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxHops = 4;
    const Path p = must(RoutingEngine{}.findPath(t, A, NodeId{5}, c));
    EXPECT_EQ(p.hopCount(), 4u);
    EXPECT_DOUBLE_EQ(p.cost(), 4.0);
}

TEST(RoutingMaxHops, LimitJustBelowTheBoundaryOfAChainIsInfeasible) {
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1), routedLink(3, 3, 4, 1, 1)});
    auto c = admin();
    c.maxHops = 3;
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, D, c)).hopCount(), 3u);
    c.maxHops = 2;
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, D, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingMaxHops, ZeroHopsAllowsOnlyTheTrivialPath) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxHops = 0;
    EXPECT_TRUE(must(RoutingEngine{}.findPath(t, A, A, c)).isTrivial());
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingMaxHops, PicksTheCheapestPathWithinEachLimit) {
    // Routes from 1 to 6: 1 hop cost 10, 2 hops cost 6, 3 hops cost 3.
    const Topology t = makeTopology({1, 2, 3, 4, 5, 6},
                                    {routedLink(1, 1, 6, 1, 10), routedLink(2, 1, 2, 1, 3), routedLink(3, 2, 6, 1, 3),
                                     routedLink(4, 1, 3, 1, 1), routedLink(5, 3, 4, 1, 1), routedLink(6, 4, 6, 1, 1)});
    const std::map<std::uint32_t, double> expectedCost = {{1, 10.0}, {2, 6.0}, {3, 3.0}, {4, 3.0}, {50, 3.0}};
    for (const auto& [limit, cost] : expectedCost) {
        auto c = admin();
        c.maxHops = limit;
        const Path p = must(RoutingEngine{}.findPath(t, A, NodeId{6}, c));
        EXPECT_DOUBLE_EQ(p.cost(), cost) << "maxHops " << limit;
        EXPECT_LE(p.hopCount(), limit);
    }
}

TEST(RoutingMaxHops, CombinesWithMaxCost) {
    const Topology t = chainWithExpress();
    auto c = admin();
    c.maxHops = 1;
    c.maxCost = 9.0;  // the only 1-hop route costs 10
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, NodeId{5}, c), ErrorCode::NoFeasibleRoute);
    c.maxCost = 10.0;
    EXPECT_TRUE(RoutingEngine{}.findPath(t, A, NodeId{5}, c).ok());
}

TEST(RoutingMaxHops, WorksWithDirectedLinksAndOtherMetrics) {
    // 1 -> 2 -> 3 -> 4 directed chain (1 km each) and a 1 -> 4 express of 50 km.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Directed),
                                                    routedLink(2, 2, 3, 1, 1, LinkDirection::Directed),
                                                    routedLink(3, 3, 4, 1, 1, LinkDirection::Directed), routedLink(4, 1, 4, 50, 1)});
    RoutingConstraints c;
    c.metric = CostMetric::Distance;
    c.maxHops = 2;
    const Path p = must(RoutingEngine{}.findPath(t, A, D, c));
    EXPECT_EQ(p.links(), linkIds({4}));
    EXPECT_DOUBLE_EQ(p.cost(), 50.0);
}

// ------------------------------------------------------------ static capacity

TEST(RoutingCapacity, LinksBelowTheMinimumCapacityAreNotUsed) {
    // Cheap route 1-2-4 has 10-channel links; the dearer route 1-3-4 has 40-channel links.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 10),
                                                    routedLink(2, 2, 4, 1, 1, LinkDirection::Bidirectional, 10),
                                                    routedLink(3, 1, 3, 1, 5), routedLink(4, 3, 4, 1, 5)});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, D, admin())).nodes(), nodeIds({1, 2, 4}));

    auto c = admin();
    c.minLinkCapacityChannels = 10;
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, D, c)).nodes(), nodeIds({1, 2, 4})) << "exactly at the boundary";
    c.minLinkCapacityChannels = 11;
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, D, c)).nodes(), nodeIds({1, 3, 4}));
    c.minLinkCapacityChannels = 41;
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, D, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingCapacity, EachHopMustMeetTheMinimum) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 40),
                                                routedLink(2, 2, 3, 1, 1, LinkDirection::Bidirectional, 4)});
    auto c = admin();
    c.minLinkCapacityChannels = 8;
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, C, c), ErrorCode::NoFeasibleRoute) << "the weakest link decides";
}

// ----------------------------------------------------- blocked nodes and links

TEST(RoutingBlocked, BlockedIntermediateNodeForcesAnAlternative) {
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 4, 1, 1), routedLink(3, 1, 3, 1, 4),
                                                   routedLink(4, 3, 4, 1, 4)});
    auto c = admin();
    c.blockedNodes.insert(B);
    const Path p = must(RoutingEngine{}.findPath(t, A, D, c));
    EXPECT_EQ(p.nodes(), nodeIds({1, 3, 4}));
    c.blockedNodes.insert(C);
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, D, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingBlocked, BlockedSourceOrDestinationIsInfeasible) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1)});
    auto c = admin();
    c.blockedNodes = {A};
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
    c.blockedNodes = {B};
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
    c.blockedNodes = {A};
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, A, c), ErrorCode::NoFeasibleRoute) << "trivial path through a blocked node is not allowed";
    c.blockedNodes = {B};
    EXPECT_TRUE(RoutingEngine{}.findPath(t, A, A, c).ok()) << "blocking some other node does not affect the trivial path";
    c.blockedNodes = {NodeId{99}};
    EXPECT_TRUE(RoutingEngine{}.findPath(t, A, B, c).ok()) << "blocking a node that does not exist is harmless";
}

TEST(RoutingBlocked, BlockedLinkForcesTheOtherParallelLink) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 1, 2, 1, 5)});
    auto c = admin();
    c.blockedLinks.insert(LinkId{1});
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, B, c)).links(), linkIds({2}));
    c.blockedLinks.insert(LinkId{2});
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingBlocked, BlockingASegmentOfTheOnlyRouteIsInfeasibleNotNoRoute) {
    const Topology t = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1)});
    auto c = admin();
    c.blockedLinks.insert(LinkId{2});
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, C, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingBlocked, ProtectionPathAvoidsTheWorkingPath) {
    // Typical use: route a backup after blocking the links of the primary path.
    const Topology t = makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 4, 1, 1), routedLink(3, 1, 3, 1, 3),
                                                   routedLink(4, 3, 4, 1, 3)});
    const Path primary = must(RoutingEngine{}.findPath(t, A, D, admin()));
    auto c = admin();
    c.blockedLinks.insert(primary.links().begin(), primary.links().end());
    const Path backup = must(RoutingEngine{}.findPath(t, A, D, c));
    EXPECT_NE(backup.links(), primary.links());
    for (LinkId l : backup.links()) EXPECT_FALSE(c.blockedLinks.contains(l));
}

// -------------------------------------------------------------- link filter

TEST(RoutingFilter, FilterCanVetoLinks_ThePhase4Seam) {
    // Simulates a resource manager saying "link 1 has no free capacity".
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 1, 2, 1, 9)});
    const std::map<std::uint32_t, std::uint32_t> freeChannels = {{1, 0}, {2, 12}};
    auto c = admin();
    c.linkFilter = [&freeChannels](const FiberLink& l) { return freeChannels.at(l.id().value()) > 0; };
    EXPECT_EQ(must(RoutingEngine{}.findPath(t, A, B, c)).links(), linkIds({2}));
}

TEST(RoutingFilter, FilterThatRejectsEverythingIsInfeasible) {
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1)});
    auto c = admin();
    c.linkFilter = [](const FiberLink&) { return false; };
    ASSERT_ERROR(RoutingEngine{}.findPath(t, A, B, c), ErrorCode::NoFeasibleRoute);
}

TEST(RoutingFilter, FilterIsNotConsultedForTrivialPaths) {
    const Topology t = makeTopology({1}, {});
    auto c = admin();
    bool called = false;
    c.linkFilter = [&called](const FiberLink&) {
        called = true;
        return true;
    };
    EXPECT_TRUE(RoutingEngine{}.findPath(t, A, A, c).ok());
    EXPECT_FALSE(called);
}

TEST(RoutingConstraints, UnconstrainedDetection) {
    RoutingConstraints c;
    EXPECT_TRUE(c.isUnconstrained());
    c.metric = CostMetric::Distance;
    EXPECT_TRUE(c.isUnconstrained()) << "the metric is not a restriction";
    c.maxHops = 3;
    EXPECT_FALSE(c.isUnconstrained());
}

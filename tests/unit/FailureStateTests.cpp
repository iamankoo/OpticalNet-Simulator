#include <gtest/gtest.h>

#include <limits>
#include <set>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/failures/FailureState.hpp"
#include "opticalnet/simulation/RandomFailures.hpp"
#include "opticalnet/simulation/SimulationEvent.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// 1 --l1-- 2 --l2-- 3 --l3-- 4
Topology chain4() {
    return makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1), routedLink(3, 3, 4, 1, 1)});
}

Path pathOver(std::initializer_list<std::uint32_t> nodes, std::initializer_list<std::uint32_t> links) {
    return must(Path::create(nodeIds(nodes), linkIds(links), static_cast<double>(links.size()), CostMetric::HopCount));
}

Topology grid4() {
    Topology t;
    for (std::uint32_t i = 1; i <= 16; ++i) EXPECT_TRUE(t.addNode(makeNode(i)).ok());
    std::uint32_t link = 1;
    for (std::uint32_t r = 0; r < 4; ++r)
        for (std::uint32_t c = 0; c < 4; ++c) {
            const std::uint32_t n = r * 4 + c + 1;
            if (c < 3) {
                EXPECT_TRUE(t.addLink(routedLink(link++, n, n + 1, 1, 1)).ok());
            }
            if (r < 3) {
                EXPECT_TRUE(t.addLink(routedLink(link++, n, n + 4, 1, 1)).ok());
            }
        }
    return t;
}

}  // namespace

// ================================================================== link failures

TEST(FailureState, StartsWithEverythingHealthy) {
    const Topology t = chain4();
    const FailureState f(t);
    EXPECT_FALSE(f.anyFailed());
    EXPECT_TRUE(f.failedLinks().empty());
    EXPECT_TRUE(f.failedNodes().empty());
    EXPECT_FALSE(f.isLinkFailed(LinkId{1}));
    EXPECT_TRUE(f.isLinkUsable(*t.findLink(LinkId{1})));
}

TEST(FailureState, FailAndRecoverAValidLink) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failLink(LinkId{2}));
    EXPECT_TRUE(f.isLinkFailed(LinkId{2}));
    EXPECT_FALSE(f.isLinkUsable(*t.findLink(LinkId{2})));
    EXPECT_TRUE(f.isLinkUsable(*t.findLink(LinkId{1}))) << "other links are unaffected";
    EXPECT_EQ(f.failedLinks(), (std::set<LinkId>{LinkId{2}}));
    ASSERT_OK(f.recoverLink(LinkId{2}));
    EXPECT_FALSE(f.isLinkFailed(LinkId{2}));
    EXPECT_TRUE(f.isLinkUsable(*t.findLink(LinkId{2})));
    EXPECT_FALSE(f.anyFailed());
}

TEST(FailureState, UnknownLinkIsNotFoundForBothTransitions) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_ERROR(f.failLink(LinkId{99}), ErrorCode::NotFound);
    ASSERT_ERROR(f.recoverLink(LinkId{99}), ErrorCode::NotFound);
    EXPECT_FALSE(f.anyFailed());
}

TEST(FailureState, RepeatedFailureIsAnExplicitError) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failLink(LinkId{1}));
    ASSERT_ERROR(f.failLink(LinkId{1}), ErrorCode::InvalidTransition);
    EXPECT_EQ(f.failedLinks().size(), 1u);
}

TEST(FailureState, RecoveringAHealthyLinkIsAnExplicitError) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_ERROR(f.recoverLink(LinkId{1}), ErrorCode::InvalidTransition);
    ASSERT_OK(f.failLink(LinkId{1}));
    ASSERT_OK(f.recoverLink(LinkId{1}));
    ASSERT_ERROR(f.recoverLink(LinkId{1}), ErrorCode::InvalidTransition) << "repeated recovery";
}

TEST(FailureState, AResourceCanFailAgainAfterRecovering) {
    const Topology t = chain4();
    FailureState f(t);
    for (int i = 0; i < 3; ++i) {
        ASSERT_OK(f.failLink(LinkId{3}));
        ASSERT_OK(f.recoverLink(LinkId{3}));
    }
    EXPECT_FALSE(f.anyFailed());
}

// ================================================================== node failures

TEST(FailureState, FailAndRecoverAValidNode) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_TRUE(f.isNodeFailed(NodeId{2}));
    EXPECT_FALSE(f.isNodeUsable(NodeId{2}));
    EXPECT_TRUE(f.isNodeUsable(NodeId{1}));
    EXPECT_EQ(f.failedNodes(), (std::set<NodeId>{NodeId{2}}));
    ASSERT_OK(f.recoverNode(NodeId{2}));
    EXPECT_FALSE(f.isNodeFailed(NodeId{2}));
}

TEST(FailureState, NodeTransitionErrors) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_ERROR(f.failNode(NodeId{99}), ErrorCode::NotFound);
    ASSERT_ERROR(f.recoverNode(NodeId{99}), ErrorCode::NotFound);
    ASSERT_ERROR(f.recoverNode(NodeId{2}), ErrorCode::InvalidTransition);
    ASSERT_OK(f.failNode(NodeId{2}));
    ASSERT_ERROR(f.failNode(NodeId{2}), ErrorCode::InvalidTransition);
}

TEST(FailureState, LinkAndNodeIdsAreIndependentNamespaces) {
    // Node 2 and link 2 both exist; failing one must not affect the other.
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_FALSE(f.isLinkFailed(LinkId{2}));
    ASSERT_OK(f.failLink(LinkId{2}));
    ASSERT_OK(f.recoverNode(NodeId{2}));
    EXPECT_TRUE(f.isLinkFailed(LinkId{2}));
    EXPECT_FALSE(f.isNodeFailed(NodeId{2}));
}

TEST(FailureState, FailedNodeMakesEveryIncidentLinkUnusableWithoutMarkingThemFailed) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_FALSE(f.isLinkUsable(*t.findLink(LinkId{1})));  // 1-2
    EXPECT_FALSE(f.isLinkUsable(*t.findLink(LinkId{2})));  // 2-3
    EXPECT_TRUE(f.isLinkUsable(*t.findLink(LinkId{3})));   // 3-4 does not touch node 2
    EXPECT_FALSE(f.isLinkFailed(LinkId{1}));
    EXPECT_TRUE(f.failedLinks().empty());
    ASSERT_OK(f.recoverNode(NodeId{2}));
    EXPECT_TRUE(f.isLinkUsable(*t.findLink(LinkId{1}))) << "recovering the node restores its links";
}

TEST(FailureState, ClearFailuresRecoversEverything) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failLink(LinkId{1}));
    ASSERT_OK(f.failNode(NodeId{3}));
    EXPECT_TRUE(f.anyFailed());
    f.clearFailures();
    EXPECT_FALSE(f.anyFailed());
    ASSERT_OK(f.failLink(LinkId{1})) << "a cleared resource can fail again";
}

TEST(FailureState, AccessorsAreOrderedById) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failLink(LinkId{3}));
    ASSERT_OK(f.failLink(LinkId{1}));
    ASSERT_OK(f.failNode(NodeId{4}));
    ASSERT_OK(f.failNode(NodeId{2}));
    EXPECT_EQ(std::vector<LinkId>(f.failedLinks().begin(), f.failedLinks().end()), linkIds({1, 3}));
    EXPECT_EQ(std::vector<NodeId>(f.failedNodes().begin(), f.failedNodes().end()), nodeIds({2, 4}));
}

// ================================================================== path impact

TEST(FailureState, PathAffectedByFailedLinkOrNode) {
    const Topology t = chain4();
    FailureState f(t);
    const Path whole = pathOver({1, 2, 3, 4}, {1, 2, 3});
    EXPECT_FALSE(f.isPathAffected(whole));
    ASSERT_OK(f.failLink(LinkId{2}));
    EXPECT_TRUE(f.isPathAffected(whole));
    EXPECT_FALSE(f.isPathAffected(pathOver({1, 2}, {1}))) << "a path that avoids the failed link is untouched";
    ASSERT_OK(f.recoverLink(LinkId{2}));
    EXPECT_FALSE(f.isPathAffected(whole));

    ASSERT_OK(f.failNode(NodeId{3}));
    EXPECT_TRUE(f.isPathAffected(whole)) << "passes through the failed node";
    EXPECT_TRUE(f.isPathAffected(pathOver({2, 3}, {2}))) << "ends at the failed node";
    EXPECT_TRUE(f.isPathAffected(pathOver({3, 4}, {3}))) << "starts at the failed node";
    EXPECT_FALSE(f.isPathAffected(pathOver({1, 2}, {1})));
}

TEST(FailureState, FailuresNeverModifyTheTopology) {
    const Topology t = chain4();
    FailureState f(t);
    ASSERT_OK(f.failLink(LinkId{2}));
    ASSERT_OK(f.failNode(NodeId{3}));
    EXPECT_EQ(t.nodeCount(), 4u);
    EXPECT_EQ(t.linkCount(), 3u);
    EXPECT_TRUE(t.hasLink(LinkId{2})) << "a failed link is unavailable, not deleted";
    EXPECT_TRUE(t.hasNode(NodeId{3}));
    EXPECT_EQ(t.degree(NodeId{3}), 2u);
    EXPECT_DOUBLE_EQ(t.findLink(LinkId{2})->lengthKm(), 1.0);
    EXPECT_EQ(t.findLink(LinkId{2})->capacityChannels(), 40u) << "configured capacity is untouched";
}

TEST(FailureState, TwoOverlaysOnOneTopologyAreIndependent) {
    const Topology t = chain4();
    FailureState a(t), b(t);
    ASSERT_OK(a.failLink(LinkId{1}));
    EXPECT_FALSE(b.isLinkFailed(LinkId{1}));
    ASSERT_OK(b.failLink(LinkId{1})) << "not 'already failed' in the other overlay";
}

// ================================================================== schedules

TEST(FailureSchedule, ValidScheduleIsAccepted) {
    const Topology t = chain4();
    const std::vector<ScriptedFailure> s = {{10, FailureAction::FailLink, 2}, {20, FailureAction::RecoverLink, 2},
                                            {30, FailureAction::FailNode, 3}, {40, FailureAction::RecoverNode, 3}};
    EXPECT_TRUE(validateFailureSchedule(t, s).ok());
    EXPECT_TRUE(validateFailureSchedule(t, {}).ok());
}

TEST(FailureSchedule, ListOrderDoesNotMatterOnlyTimeDoes) {
    const Topology t = chain4();
    const std::vector<ScriptedFailure> s = {{20, FailureAction::RecoverLink, 2}, {10, FailureAction::FailLink, 2}};
    EXPECT_TRUE(validateFailureSchedule(t, s).ok());
}

TEST(FailureSchedule, RejectsUnknownIdsAndBadTransitions) {
    const Topology t = chain4();
    using V = std::vector<ScriptedFailure>;
    ASSERT_ERROR(validateFailureSchedule(t, V{{1, FailureAction::FailLink, 99}}), ErrorCode::NotFound);
    ASSERT_ERROR(validateFailureSchedule(t, V{{1, FailureAction::FailNode, 99}}), ErrorCode::NotFound);
    ASSERT_ERROR(validateFailureSchedule(t, V{{1, FailureAction::RecoverLink, 1}}), ErrorCode::InvalidTransition);
    ASSERT_ERROR(validateFailureSchedule(t, V{{1, FailureAction::FailLink, 1}, {2, FailureAction::FailLink, 1}}), ErrorCode::InvalidTransition);
    ASSERT_ERROR(validateFailureSchedule(t, V{{1, FailureAction::FailNode, 1}, {5, FailureAction::RecoverNode, 1}, {6, FailureAction::RecoverNode, 1}}),
                 ErrorCode::InvalidTransition);
}

TEST(FailureSchedule, RejectsInvalidTimes) {
    const Topology t = chain4();
    using V = std::vector<ScriptedFailure>;
    ASSERT_ERROR(validateFailureSchedule(t, V{{-1, FailureAction::FailLink, 1}}), ErrorCode::InvalidArgument);
    ASSERT_ERROR(validateFailureSchedule(t, V{{std::numeric_limits<double>::quiet_NaN(), FailureAction::FailLink, 1}}), ErrorCode::InvalidArgument);
}

TEST(FailureSchedule, ErrorMessageNamesTheOffendingEntry) {
    const Topology t = chain4();
    const auto r = validateFailureSchedule(t, std::vector<ScriptedFailure>{{1, FailureAction::FailLink, 1}, {2, FailureAction::FailLink, 1}});
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("entry 1"), std::string::npos) << r.error().message;
}

TEST(FailureSchedule, SameTimeRecoveryIsAppliedBeforeFailure) {
    const Topology t = chain4();
    using V = std::vector<ScriptedFailure>;
    // fail@2, then at t=5 recover and fail again: legal because the recovery takes effect first.
    EXPECT_TRUE(validateFailureSchedule(t, V{{2, FailureAction::FailLink, 1}, {5, FailureAction::FailLink, 1}, {5, FailureAction::RecoverLink, 1}}).ok());
    // fail and recover at the same instant: the recovery would come first, so it is a recovery of a healthy link.
    ASSERT_ERROR(validateFailureSchedule(t, V{{5, FailureAction::FailLink, 1}, {5, FailureAction::RecoverLink, 1}}), ErrorCode::InvalidTransition);
}

TEST(FailureSchedule, ActionPrioritiesAndNames) {
    EXPECT_LT(failureActionPriority(FailureAction::RecoverLink), failureActionPriority(FailureAction::FailLink));
    EXPECT_LT(failureActionPriority(FailureAction::RecoverNode), failureActionPriority(FailureAction::FailNode));
    EXPECT_LT(failureActionPriority(FailureAction::RecoverLink), failureActionPriority(FailureAction::FailNode));
    EXPECT_EQ(toString(RecoveryPolicy::Reroute), "reroute");
    EXPECT_EQ(toString(RecoveryPolicy::NoRecovery), "no_recovery");
    EXPECT_EQ(toString(FailureAction::FailLink), "fail_link");
}

TEST(FailureSchedule, RandomConfigValidation) {
    EXPECT_TRUE(RandomFailureConfig{}.validate().ok());
    const auto bad = [](auto mutate) {
        RandomFailureConfig c;
        mutate(c);
        const auto r = c.validate();
        return !r.ok() && r.error().code == ErrorCode::InvalidArgument;
    };
    EXPECT_TRUE(bad([](RandomFailureConfig& c) { c.failureRate = -1; }));
    EXPECT_TRUE(bad([](RandomFailureConfig& c) { c.meanRepairTime = 0; }));
    EXPECT_TRUE(bad([](RandomFailureConfig& c) { c.nodeFailureProbability = 1.5; }));
    EXPECT_TRUE(bad([](RandomFailureConfig& c) { c.nodeFailureProbability = -0.1; }));
}

// ================================================================== event ordering

TEST(FailureEvents, SameTimestampOrderIsRecoveriesFailuresReleasesArrivals) {
    EventQueue q;
    ASSERT_OK(q.schedule(4.0, EventType::ConnectionArrival, ConnectionId{1}));
    ASSERT_OK(q.scheduleResource(4.0, EventType::NodeFailure, 7));
    ASSERT_OK(q.schedule(4.0, EventType::ConnectionRelease, ConnectionId{2}));
    ASSERT_OK(q.scheduleResource(4.0, EventType::LinkFailure, 8));
    ASSERT_OK(q.scheduleResource(4.0, EventType::NodeRecovery, 9));
    ASSERT_OK(q.scheduleResource(4.0, EventType::LinkRecovery, 10));
    std::vector<EventType> order;
    while (!q.empty()) order.push_back(q.pop().type);
    EXPECT_EQ(order, (std::vector<EventType>{EventType::LinkRecovery, EventType::NodeRecovery, EventType::LinkFailure, EventType::NodeFailure,
                                             EventType::ConnectionRelease, EventType::ConnectionArrival}));
}

TEST(FailureEvents, TimeStillBeatsType) {
    EventQueue q;
    ASSERT_OK(q.scheduleResource(9.0, EventType::LinkRecovery, 1));
    ASSERT_OK(q.schedule(3.0, EventType::ConnectionArrival, ConnectionId{1}));
    EXPECT_EQ(q.pop().type, EventType::ConnectionArrival);
}

TEST(FailureEvents, ResourceIdTravelsWithTheEventAndSequenceBreaksTies) {
    EventQueue q;
    ASSERT_OK(q.scheduleResource(2.0, EventType::LinkFailure, 11));
    ASSERT_OK(q.scheduleResource(2.0, EventType::LinkFailure, 5));
    EXPECT_EQ(q.pop().resource, 11u) << "scheduling order, not id order";
    EXPECT_EQ(q.pop().resource, 5u);
}

TEST(FailureEvents, InvalidTimesAreRejectedAndClassificationWorks) {
    EventQueue q;
    ASSERT_ERROR(q.scheduleResource(-1.0, EventType::LinkFailure, 1), ErrorCode::InvalidArgument);
    EXPECT_TRUE(isFailureEvent(EventType::NodeRecovery));
    EXPECT_FALSE(isFailureEvent(EventType::ConnectionArrival));
    EXPECT_FALSE(isFailureEvent(EventType::ConnectionRelease));
}

// ================================================================== random failure generator

TEST(RandomFailures, SameSeedGivesTheSameSchedule) {
    const Topology t = grid4();
    RandomFailureConfig c;
    c.failureRate = 0.2;
    c.meanRepairTime = 8;
    c.nodeFailureProbability = 0.3;
    EXPECT_EQ(generateRandomFailures(t, c, 200, 42), generateRandomFailures(t, c, 200, 42));
}

TEST(RandomFailures, DifferentSeedsGiveDifferentSchedules) {
    const Topology t = grid4();
    RandomFailureConfig c;
    c.failureRate = 0.2;
    EXPECT_NE(generateRandomFailures(t, c, 200, 1), generateRandomFailures(t, c, 200, 2));
}

TEST(RandomFailures, ScheduleIsValidAndOnlyUsesExistingIds) {
    const Topology t = grid4();
    RandomFailureConfig c;
    c.failureRate = 0.5;
    c.meanRepairTime = 6;
    c.nodeFailureProbability = 0.4;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const auto schedule = generateRandomFailures(t, c, 300, seed);
        ASSERT_FALSE(schedule.empty());
        EXPECT_TRUE(validateFailureSchedule(t, schedule).ok()) << "seed " << seed;
        for (const auto& f : schedule) {
            EXPECT_LE(f.time, 300.0);
            EXPECT_GT(f.time, 0.0);
            const bool node = f.action == FailureAction::FailNode || f.action == FailureAction::RecoverNode;
            EXPECT_TRUE(node ? t.hasNode(NodeId{f.id}) : t.hasLink(LinkId{f.id}));
        }
    }
}

TEST(RandomFailures, RecoveriesNeverOutnumberFailures) {
    const Topology t = grid4();
    RandomFailureConfig c;
    c.failureRate = 0.3;
    const auto schedule = generateRandomFailures(t, c, 400, 7);
    std::size_t failures = 0, recoveries = 0;
    for (const auto& f : schedule) {
        (f.action == FailureAction::FailLink || f.action == FailureAction::FailNode ? failures : recoveries)++;
    }
    EXPECT_GT(failures, 10u);
    EXPECT_LE(recoveries, failures) << "recoveries beyond the horizon are not generated";
    EXPECT_GT(recoveries, 0u);
}

TEST(RandomFailures, NodeProbabilityControlsTheKindOfResourceHit) {
    const Topology t = grid4();
    RandomFailureConfig c;
    c.failureRate = 0.4;
    c.nodeFailureProbability = 0.0;
    for (const auto& f : generateRandomFailures(t, c, 300, 3))
        EXPECT_TRUE(f.action == FailureAction::FailLink || f.action == FailureAction::RecoverLink);
    c.nodeFailureProbability = 1.0;
    for (const auto& f : generateRandomFailures(t, c, 300, 3))
        EXPECT_TRUE(f.action == FailureAction::FailNode || f.action == FailureAction::RecoverNode);
}

TEST(RandomFailures, ZeroRateOrEmptyTopologyGivesNothing) {
    RandomFailureConfig c;
    EXPECT_TRUE(generateRandomFailures(grid4(), c, 100, 1).empty());
    c.failureRate = 1.0;
    EXPECT_TRUE(generateRandomFailures(Topology{}, c, 100, 1).empty());
    EXPECT_TRUE(generateRandomFailures(grid4(), c, 0.0, 1).empty()) << "zero horizon";
}

TEST(RandomFailures, ASingleResourceNeverFailsTwiceAtOnce) {
    // One link only, frequent failures and slow repair: the generator must skip failures while it is down.
    const Topology t = makeTopology({1, 2}, {routedLink(1, 1, 2, 1, 1)});
    RandomFailureConfig c;
    c.failureRate = 5.0;
    c.meanRepairTime = 20.0;
    const auto schedule = generateRandomFailures(t, c, 200, 9);
    EXPECT_TRUE(validateFailureSchedule(t, schedule).ok());
}

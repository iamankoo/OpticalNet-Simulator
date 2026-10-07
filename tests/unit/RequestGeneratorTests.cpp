#include <gtest/gtest.h>

#include <set>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/simulation/RequestGenerator.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// 5 nodes in a line (the topology only matters for its node ids here).
Topology line5() {
    return makeTopology({1, 2, 3, 4, 5}, {routedLink(1, 1, 2, 1, 1), routedLink(2, 2, 3, 1, 1), routedLink(3, 3, 4, 1, 1),
                                          routedLink(4, 4, 5, 1, 1)});
}

SimulationConfig baseConfig() {
    SimulationConfig c;
    c.seed = 42;
    c.endTime = 1000.0;
    c.arrivalRate = 1.0;
    c.meanLifetime = 5.0;
    c.maxRequests = 500;
    return c;
}

std::vector<GeneratedRequest> drain(RequestGenerator& g) {
    std::vector<GeneratedRequest> all;
    while (auto next = g.next()) all.push_back(std::move(*next));
    return all;
}

}  // namespace

TEST(RequestGenerator, EndpointsAreValidNodesAndNeverEqual) {
    const Topology t = line5();
    auto gen = must(RequestGenerator::create(t, baseConfig()));
    const auto requests = drain(gen);
    ASSERT_EQ(requests.size(), 500u);
    for (const auto& r : requests) {
        EXPECT_TRUE(t.hasNode(r.request.source));
        EXPECT_TRUE(t.hasNode(r.request.destination));
        EXPECT_NE(r.request.source, r.request.destination) << "self-connections are never generated";
        EXPECT_TRUE(r.request.validate().ok());
    }
}

TEST(RequestGenerator, EveryNodeIsUsedAsSourceAndDestination) {
    const Topology t = line5();
    auto gen = must(RequestGenerator::create(t, baseConfig()));
    std::set<NodeId> sources, destinations;
    for (const auto& r : drain(gen)) {
        sources.insert(r.request.source);
        destinations.insert(r.request.destination);
    }
    EXPECT_EQ(sources.size(), 5u);
    EXPECT_EQ(destinations.size(), 5u);
}

TEST(RequestGenerator, TwoNodeTopologyAlwaysPicksTheOnlyPair) {
    const Topology t = makeTopology({7, 9}, {routedLink(1, 7, 9, 1, 1)});
    auto gen = must(RequestGenerator::create(t, baseConfig()));
    for (const auto& r : drain(gen)) {
        EXPECT_TRUE((r.request.source == NodeId{7} && r.request.destination == NodeId{9}) ||
                    (r.request.source == NodeId{9} && r.request.destination == NodeId{7}));
    }
}

TEST(RequestGenerator, CapacityStaysWithinConfiguredBounds) {
    const Topology t = line5();
    auto c = baseConfig();
    c.minCapacityChannels = 2;
    c.maxCapacityChannels = 5;
    auto gen = must(RequestGenerator::create(t, c));
    std::set<std::uint32_t> seen;
    for (const auto& r : drain(gen)) {
        EXPECT_GE(r.request.capacityChannels, 2u);
        EXPECT_LE(r.request.capacityChannels, 5u);
        seen.insert(r.request.capacityChannels);
    }
    EXPECT_EQ(seen.size(), 4u) << "every value of the range occurs";
}

TEST(RequestGenerator, FixedCapacityWhenMinEqualsMax) {
    const Topology t = line5();
    auto c = baseConfig();
    c.minCapacityChannels = c.maxCapacityChannels = 3;
    auto gen = must(RequestGenerator::create(t, c));
    for (const auto& r : drain(gen)) EXPECT_EQ(r.request.capacityChannels, 3u);
}

TEST(RequestGenerator, IdsAreSequentialFromOne) {
    const Topology t = line5();
    auto gen = must(RequestGenerator::create(t, baseConfig()));
    std::uint32_t expected = 1;
    for (const auto& r : drain(gen)) EXPECT_EQ(r.request.id, ConnectionId{expected++});
    EXPECT_EQ(gen.generated(), 500u);
}

TEST(RequestGenerator, ArrivalTimesStrictlyIncrease) {
    const Topology t = line5();
    auto gen = must(RequestGenerator::create(t, baseConfig()));
    SimTime previous = 0.0;
    for (const auto& r : drain(gen)) {
        EXPECT_GT(r.arrival, previous);
        EXPECT_GT(r.lifetime, 0.0);
        previous = r.arrival;
    }
}

TEST(RequestGenerator, RoutingConstraintsAreCopiedIntoEveryRequest) {
    const Topology t = line5();
    auto c = baseConfig();
    c.routing.metric = CostMetric::Distance;
    c.routing.maxHops = 3;
    c.routing.blockedNodes.insert(NodeId{4});
    auto gen = must(RequestGenerator::create(t, c));
    for (const auto& r : drain(gen)) {
        EXPECT_EQ(r.request.constraints.metric, CostMetric::Distance);
        EXPECT_EQ(r.request.constraints.maxHops, 3u);
        EXPECT_TRUE(r.request.constraints.blockedNodes.contains(NodeId{4}));
    }
}

TEST(RequestGenerator, SameSeedSameWorkload) {
    const Topology t = line5();
    auto a = must(RequestGenerator::create(t, baseConfig()));
    auto b = must(RequestGenerator::create(t, baseConfig()));
    const auto x = drain(a), y = drain(b);
    ASSERT_EQ(x.size(), y.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        EXPECT_EQ(x[i].arrival, y[i].arrival);
        EXPECT_EQ(x[i].lifetime, y[i].lifetime);
        EXPECT_EQ(x[i].request.source, y[i].request.source);
        EXPECT_EQ(x[i].request.destination, y[i].request.destination);
        EXPECT_EQ(x[i].request.capacityChannels, y[i].request.capacityChannels);
    }
}

TEST(RequestGenerator, DifferentSeedsGiveDifferentWorkloads) {
    const Topology t = line5();
    auto c1 = baseConfig();
    auto c2 = baseConfig();
    c2.seed = 43;
    auto a = must(RequestGenerator::create(t, c1));
    auto b = must(RequestGenerator::create(t, c2));
    const auto x = drain(a), y = drain(b);
    bool differs = false;
    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i) differs = differs || x[i].arrival != y[i].arrival;
    EXPECT_TRUE(differs);
}

TEST(RequestGenerator, WorkloadDoesNotDependOnTheTopologyBeyondItsNodeIds) {
    // Same node ids, different links/capacity: the generated requests are identical.
    const Topology sparse = makeTopology({1, 2, 3, 4, 5}, {});
    const Topology dense = line5();
    auto a = must(RequestGenerator::create(sparse, baseConfig()));
    auto b = must(RequestGenerator::create(dense, baseConfig()));
    const auto x = drain(a), y = drain(b);
    ASSERT_EQ(x.size(), y.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        EXPECT_EQ(x[i].arrival, y[i].arrival);
        EXPECT_EQ(x[i].request.source, y[i].request.source);
    }
}

TEST(RequestGenerator, StopsAtTheRequestCap) {
    const Topology t = line5();
    auto c = baseConfig();
    c.maxRequests = 7;
    auto gen = must(RequestGenerator::create(t, c));
    EXPECT_FALSE(gen.limitReached());
    EXPECT_EQ(drain(gen).size(), 7u);
    EXPECT_TRUE(gen.limitReached());
    EXPECT_FALSE(gen.next().has_value());
}

TEST(RequestGenerator, StopsAtTheHorizon) {
    const Topology t = line5();
    auto c = baseConfig();
    c.maxRequests.reset();
    c.endTime = 20.0;
    auto gen = must(RequestGenerator::create(t, c));
    const auto requests = drain(gen);
    EXPECT_GT(requests.size(), 5u);
    for (const auto& r : requests) EXPECT_LE(r.arrival, 20.0);
    EXPECT_FALSE(gen.limitReached()) << "ended by the horizon, not the cap";
    EXPECT_FALSE(gen.next().has_value()) << "stays exhausted";
}

TEST(RequestGenerator, ZeroRateZeroDurationAndZeroCapProduceNothing) {
    const Topology t = line5();
    auto c = baseConfig();
    c.arrivalRate = 0.0;
    EXPECT_FALSE(must(RequestGenerator::create(t, c)).next().has_value());

    c = baseConfig();
    c.endTime = 0.0;
    EXPECT_FALSE(must(RequestGenerator::create(t, c)).next().has_value());

    c = baseConfig();
    c.maxRequests = 0;
    auto gen = must(RequestGenerator::create(t, c));
    EXPECT_FALSE(gen.next().has_value());
    EXPECT_TRUE(gen.limitReached());
}

TEST(RequestGenerator, FewerThanTwoNodesProduceNothing) {
    EXPECT_FALSE(must(RequestGenerator::create(Topology{}, baseConfig())).next().has_value());
    EXPECT_FALSE(must(RequestGenerator::create(makeTopology({1}, {}), baseConfig())).next().has_value());
}

TEST(RequestGenerator, InvalidConfigIsRejected) {
    auto c = baseConfig();
    c.meanLifetime = 0.0;
    ASSERT_ERROR(RequestGenerator::create(line5(), c), ErrorCode::InvalidArgument);
}

TEST(RequestGenerator, FixedTimingIsExact) {
    const Topology t = line5();
    auto c = baseConfig();
    c.arrivalRate = 4.0;  // a request every 0.25
    c.arrivalTiming = Timing::Fixed;
    c.meanLifetime = 2.5;
    c.lifetimeTiming = Timing::Fixed;
    c.maxRequests = 8;
    auto gen = must(RequestGenerator::create(t, c));
    double expected = 0.25;
    for (const auto& r : drain(gen)) {
        EXPECT_DOUBLE_EQ(r.arrival, expected);
        EXPECT_DOUBLE_EQ(r.lifetime, 2.5);
        expected += 0.25;
    }
}

TEST(RequestGenerator, ExponentialMeansMatchTheConfiguration) {
    const Topology t = line5();
    auto c = baseConfig();
    c.arrivalRate = 2.0;  // mean gap 0.5
    c.meanLifetime = 8.0;
    c.endTime = 1e9;
    c.maxRequests = 20000;
    auto gen = must(RequestGenerator::create(t, c));
    const auto requests = drain(gen);
    ASSERT_EQ(requests.size(), 20000u);
    double lifetimeSum = 0.0;
    for (const auto& r : requests) lifetimeSum += r.lifetime;
    EXPECT_NEAR(requests.back().arrival / 20000.0, 0.5, 0.025) << "mean inter-arrival time = 1 / rate";
    EXPECT_NEAR(lifetimeSum / 20000.0, 8.0, 0.4);
}

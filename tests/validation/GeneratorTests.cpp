#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(ValGenerators, TopologiesAreDeterministicForASeed) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed)
        EXPECT_EQ(fingerprint(generateRandomTopology(RandomTopologyConfig{}, seed)), fingerprint(generateRandomTopology(RandomTopologyConfig{}, seed))) << seed;
}

TEST(ValGenerators, DifferentSeedsProduceVariety) {
    std::set<std::string> distinct;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) distinct.insert(fingerprint(generateRandomTopology(RandomTopologyConfig{}, seed)));
    EXPECT_GT(distinct.size(), 25u);
}

TEST(ValGenerators, TopologiesRespectTheirConfiguration) {
    RandomTopologyConfig cfg;
    cfg.minNodes = 5;
    cfg.maxNodes = 9;
    cfg.minCapacity = 3;
    cfg.maxCapacity = 4;
    cfg.maxLengthKm = 6;
    cfg.maxAdminCost = 2;
    std::size_t directed = 0, parallel = 0, total = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        const Topology t = generateRandomTopology(cfg, seed);
        EXPECT_GE(t.nodeCount(), 5u);
        EXPECT_LE(t.nodeCount(), 9u);
        std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
        for (const auto& [id, link] : t.network().links()) {
            EXPECT_GE(link.capacityChannels(), 3u);
            EXPECT_LE(link.capacityChannels(), 4u);
            EXPECT_GE(link.lengthKm(), 1.0);
            EXPECT_LE(link.lengthKm(), 6.0);
            EXPECT_LE(link.administrativeCost(), 2.0);
            EXPECT_EQ(link.lengthKm(), std::floor(link.lengthKm())) << "integer lengths keep sums exact";
            directed += link.direction() == LinkDirection::Directed;
            ++total;
            const std::uint32_t a = link.source().value(), b = link.target().value();
            if (!pairs.insert({std::min(a, b), std::max(a, b)}).second) ++parallel;
        }
        ASSERT_VALID(checkTopology(t));
    }
    EXPECT_GT(directed, total / 10) << "directed links occur";
    EXPECT_GT(parallel, 0u) << "parallel links occur";
}

TEST(ValGenerators, InvalidConfigurationsAreProgrammingErrors) {
    RandomTopologyConfig cfg;
    cfg.minNodes = 1;
    EXPECT_THROW((void)generateRandomTopology(cfg, 1), std::invalid_argument);
    cfg = {};
    cfg.minNodes = 8;
    cfg.maxNodes = 4;
    EXPECT_THROW((void)generateRandomTopology(cfg, 1), std::invalid_argument);
    cfg = {};
    cfg.minCapacity = 0;
    EXPECT_THROW((void)generateRandomTopology(cfg, 1), std::invalid_argument);
    EXPECT_THROW((void)generateRandomRequests(Topology{}, RandomRequestConfig{}, 3, 1), std::invalid_argument);
}

TEST(ValGenerators, RequestsAreValidAndDeterministic) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        const auto a = generateRandomRequests(t, RandomRequestConfig{1, 4, 0.5}, 60, seed);
        const auto b = generateRandomRequests(t, RandomRequestConfig{1, 4, 0.5}, 60, seed);
        ASSERT_EQ(a.size(), 60u);
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i].id, ConnectionId{static_cast<std::uint32_t>(i + 1)});
            EXPECT_TRUE(t.hasNode(a[i].source));
            EXPECT_TRUE(t.hasNode(a[i].destination));
            EXPECT_NE(a[i].source, a[i].destination);
            EXPECT_GE(a[i].capacityChannels, 1u);
            EXPECT_LE(a[i].capacityChannels, 4u);
            EXPECT_TRUE(a[i].validate().ok());
            EXPECT_EQ(a[i].source, b[i].source);
            EXPECT_EQ(a[i].destination, b[i].destination);
            EXPECT_EQ(a[i].capacityChannels, b[i].capacityChannels);
            EXPECT_EQ(a[i].constraints.maxHops, b[i].constraints.maxHops);
        }
    }
}

TEST(ValGenerators, ConstraintsAreValidAndVaried) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 3);
    Rng rng(1);
    int hops = 0, cost = 0, capacity = 0, nodes = 0, links = 0;
    std::set<CostMetric> metrics;
    for (int i = 0; i < 400; ++i) {
        const RoutingConstraints c = generateRandomConstraints(t, rng, 0.6);
        metrics.insert(c.metric);
        hops += c.maxHops.has_value();
        cost += c.maxCost.has_value();
        capacity += c.minLinkCapacityChannels > 0;
        for (NodeId n : c.blockedNodes) { EXPECT_TRUE(t.hasNode(n)); ++nodes; }
        for (LinkId l : c.blockedLinks) { EXPECT_TRUE(t.hasLink(l)); ++links; }
        EXPECT_FALSE(c.linkFilter) << "generated constraints are plain data";
        if (c.maxCost) { EXPECT_GE(*c.maxCost, 0.0); }
    }
    EXPECT_EQ(metrics.size(), 3u);
    EXPECT_GT(hops, 100);
    EXPECT_GT(cost, 100);
    EXPECT_GT(capacity, 30);
    EXPECT_GT(nodes, 30);
    EXPECT_GT(links, 30);
}

TEST(ValGenerators, FailureScriptsAreValidDeterministicAndBounded) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        const auto a = generateRandomFailureScript(t, 40, 60.0, seed);
        EXPECT_EQ(a, generateRandomFailureScript(t, 40, 60.0, seed));
        EXPECT_TRUE(validateFailureSchedule(t, a).ok()) << seedContext(seed);
        EXPECT_LE(a.size(), 40u);
        double previous = 0;
        for (const ScriptedFailure& f : a) {
            EXPECT_GT(f.time, previous);
            EXPECT_LE(f.time, 60.0);
            previous = f.time;
        }
    }
    EXPECT_TRUE(generateRandomFailureScript(Topology{}, 10, 10.0, 1).empty());
    EXPECT_TRUE(generateRandomFailureScript(generateRandomTopology(RandomTopologyConfig{}, 1), 10, 0.5, 1).empty()) << "nothing fits before the horizon";
}

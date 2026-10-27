// Bounded stress tests: correctness and invariant preservation at larger scale, not performance measurement.
#include <deque>
#include <limits>

#include "ValidationTestUtils.hpp"
#include "opticalnet/routing/DijkstraRouter.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// A big connected topology: a ring of n nodes (so everything is reachable both ways) plus random chords.
Topology bigTopology(std::uint32_t nodes, std::uint32_t chords, std::uint64_t seed, std::uint32_t capacity) {
    Topology t;
    for (std::uint32_t i = 1; i <= nodes; ++i) EXPECT_TRUE(t.addNode(makeNode(i)).ok());
    Rng rng(seed);
    std::uint32_t link = 1;
    for (std::uint32_t i = 1; i <= nodes; ++i)
        EXPECT_TRUE(t.addLink(routedLink(link++, i, i % nodes + 1, rng.uniformInt(1, 9), rng.uniformInt(1, 9), LinkDirection::Bidirectional, capacity)).ok());
    for (std::uint32_t k = 0; k < chords; ++k) {
        const std::uint32_t a = rng.uniformInt(1, nodes);
        std::uint32_t b = rng.uniformInt(1, nodes - 1);
        if (b >= a) ++b;
        EXPECT_TRUE(t.addLink(routedLink(link++, a, b, rng.uniformInt(1, 9), rng.uniformInt(1, 9), LinkDirection::Bidirectional, capacity)).ok());
    }
    return t;
}

// Independent shortest-path costs from `source`: queue-based Bellman-Ford over the raw link list (a different algorithm
// from the production Dijkstra, and not using the adjacency index).
std::vector<double> bellmanFord(const Topology& t, NodeId source, CostMetric metric) {
    std::vector<const FiberLink*> links;
    std::map<NodeId, std::vector<const FiberLink*>> byNode;
    for (const auto& e : t.network().links()) {
        links.push_back(&e.second);
        byNode[e.second.source()].push_back(&e.second);
        byNode[e.second.target()].push_back(&e.second);
    }
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(t.nodeCount() + 1, inf);
    dist[source.value()] = 0;
    std::deque<NodeId> queue{source};
    std::vector<bool> queued(t.nodeCount() + 1, false);
    queued[source.value()] = true;
    const auto weight = [metric](const FiberLink& l) { return metric == CostMetric::HopCount ? 1.0 : metric == CostMetric::Distance ? l.lengthKm() : l.administrativeCost(); };
    while (!queue.empty()) {
        const NodeId at = queue.front();
        queue.pop_front();
        queued[at.value()] = false;
        for (const FiberLink* l : byNode[at]) {
            NodeId next{0};
            if (l->source() == at) next = l->target();
            else if (l->direction() == LinkDirection::Bidirectional) next = l->source();
            else continue;
            if (dist[at.value()] + weight(*l) < dist[next.value()]) {
                dist[next.value()] = dist[at.value()] + weight(*l);
                if (!queued[next.value()]) {
                    queued[next.value()] = true;
                    queue.push_back(next);
                }
            }
        }
    }
    return dist;
}

}  // namespace

TEST(ValStress, RoutingOnAFiveThousandNodeGraphMatchesAnIndependentAlgorithm) {
    const Topology t = bigTopology(5000, 3000, 1, 8);
    ASSERT_VALID(checkTopology(t, false));
    Rng rng(2);
    int checked = 0;
    for (CostMetric metric : {CostMetric::HopCount, CostMetric::Distance, CostMetric::Administrative}) {
        const NodeId source{rng.uniformInt(1, 5000)};
        const std::vector<double> dist = bellmanFord(t, source, metric);
        for (int q = 0; q < 12; ++q) {
            const NodeId dest{rng.uniformInt(1, 5000)};
            RoutingConstraints c;
            c.metric = metric;
            const auto path = DijkstraRouter{}.findPath(t, source, dest, c);
            ASSERT_TRUE(path.ok()) << source.value() << "->" << dest.value() << " " << (path.ok() ? "" : path.error().describe());
            EXPECT_NEAR(path.value().cost(), dist[dest.value()], 1e-9) << "metric " << toString(metric) << ", " << source.value() << "->" << dest.value();
            EXPECT_VALID(checkPath(t, path.value(), PathExpectation{source, dest, metric, &c, nullptr}));
            ++checked;
        }
    }
    EXPECT_EQ(checked, 36);
}

TEST(ValStress, ThousandsOfAllocationsAndReleasesAgreeWithTheLedger) {
    const Topology t = bigTopology(700, 700, 3, 6);
    ResourceManager rm{t};
    ShadowLedger ledger(t);
    Rng rng(4);
    std::vector<LinkId> all;
    for (const auto& e : t.network().links()) all.push_back(e.first);
    struct Held {
        std::vector<LinkId> links;
        std::uint32_t demand;
    };
    std::vector<Held> held;
    int accepted = 0, rejected = 0;
    for (int op = 0; op < 20000; ++op) {
        if (held.empty() || rng.uniformInt(0, 2) != 0) {
            Held h;
            h.demand = rng.uniformInt(1, 3);
            const std::uint32_t length = rng.uniformInt(1, 8);
            for (std::uint32_t i = 0; i < length; ++i) h.links.push_back(all[rng.uniformIndex(all.size())]);
            const bool ok = ledger.allocate(h.links, h.demand);
            ASSERT_EQ(rm.allocate(h.links, h.demand).ok(), ok) << "op " << op;
            if (ok) { held.push_back(std::move(h)); ++accepted; } else ++rejected;
        } else {
            const std::size_t i = rng.uniformIndex(held.size());
            ASSERT_TRUE(ledger.release(held[i].links, held[i].demand));
            ASSERT_OK(rm.release(held[i].links, held[i].demand));
            held.erase(held.begin() + static_cast<long>(i));
        }
        if (op % 1000 == 0) {
            ASSERT_VALID(checkResources(rm, t));
            ASSERT_EQ(rm.totalAllocated(), ledger.totalAllocated());
        }
    }
    ASSERT_VALID(checkResources(rm, t));
    for (const LinkId id : all) ASSERT_EQ(rm.allocated(id), ledger.allocated(id));
    EXPECT_GT(accepted, 5000);
    EXPECT_GT(rejected, 1000);
}

TEST(ValStress, ThousandsOfRequestsThroughTheSimulationReplayExactly) {
    const Topology t = bigTopology(60, 60, 5, 5);
    SimulationConfig c;
    c.seed = 6;
    c.endTime = 1000.0;
    c.arrivalRate = 5.0;  // ~5000 requests
    c.meanLifetime = 12.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    const auto r = must(SimulationEngine{}.run(t, c));
    EXPECT_GT(r.metrics.totalRequests, 4500u);
    EXPECT_GT(r.metrics.rejectedRequests, 0u) << "the network must actually congest";
    ASSERT_VALID(checkSimulationTrace(t, r));
}

TEST(ValStress, ManyFailureAndRecoveryEventsKeepEverythingConsistent) {
    const Topology t = bigTopology(50, 60, 7, 6);
    const std::string before = fingerprint(t);
    SimulationConfig c;
    c.seed = 8;
    c.endTime = 600.0;
    c.arrivalRate = 4.0;
    c.meanLifetime = 15.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    RandomFailureConfig f;
    f.failureRate = 0.8;
    f.meanRepairTime = 10.0;
    f.nodeFailureProbability = 0.15;
    c.randomFailures = f;
    const auto r = must(SimulationEngine{}.run(t, c));
    EXPECT_GT(r.metrics.totalFailureEvents, 300u);
    EXPECT_GT(r.metrics.linkRecoveries + r.metrics.nodeRecoveries, 200u);
    EXPECT_GT(r.metrics.connectionsAffected, 200u);
    EXPECT_GT(r.metrics.connectionsRerouted, 50u);
    EXPECT_GT(r.metrics.connectionsLost, 20u);
    ASSERT_VALID(checkSimulationTrace(t, r));
    EXPECT_EQ(fingerprint(t), before);
}

TEST(ValStress, ParallelReplicationsWithFailuresValidateAndMatchTheSingleWorkerRun) {
    const Topology t = bigTopology(40, 50, 9, 6);
    SimulationConfig c;
    c.endTime = 300.0;
    c.arrivalRate = 3.0;
    c.meanLifetime = 12.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.recordTrace = true;
    RandomFailureConfig f;
    f.failureRate = 0.3;
    f.meanRepairTime = 12.0;
    f.nodeFailureProbability = 0.2;
    c.randomFailures = f;
    StudyConfig s;
    s.replications = 24;
    s.baseSeed = 77;
    s.workerCount = 1;
    const auto one = must(SimulationStudy::run(t, c, s));
    s.workerCount = 8;
    const auto eight = must(SimulationStudy::run(t, c, s));
    EXPECT_EQ(one.replications, eight.replications);
    EXPECT_EQ(one.aggregate, eight.aggregate);
    for (std::size_t i = 0; i < eight.replications.size(); ++i) {
        SCOPED_TRACE("replication " + std::to_string(i));
        ASSERT_VALID(checkSimulationTrace(t, eight.replications[i]));
    }
    EXPECT_GT(eight.aggregate.totalRequests, 24u * 600u);
    EXPECT_GT(eight.aggregate.totalConnectionsAffected, 100u);
}

TEST(ValStress, LargeScriptedFailureWorkloadOnALargeTopology) {
    const Topology t = bigTopology(300, 400, 11, 8);
    Rng rng(12);
    std::vector<ScriptedArrival> script;
    double time = 0;
    for (const ConnectionRequest& request : generateRandomRequests(t, RandomRequestConfig{1, 3, 0.2}, 2500, 13)) {
        time += 0.1 + static_cast<double>(rng.uniformInt(0, 2)) * 0.1;
        script.push_back({time, request, 5.0 + static_cast<double>(rng.uniformInt(0, 30))});
    }
    const auto r = runScript(t, script, tracedConfig(time + 50.0, generateRandomFailureScript(t, 150, time, 14)));
    EXPECT_GT(r.metrics.totalRequests, 2400u);
    EXPECT_GT(r.metrics.totalFailureEvents, 20u);
    ASSERT_VALID(checkSimulationTrace(t, r));
}

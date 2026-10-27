// OpticalNet benchmark suite (Phase 10).
//
//   opticalnet_bench [--quick] [--repeats N] [--json FILE] [--only SUBSTRING]
//
// Every workload is deterministic: topologies are generated from fixed formulas and every random choice
// (query endpoints, blocked nodes, simulation seeds) comes from a seeded generator, so two runs execute exactly the
// same work. Only the wall-clock times differ. Each timing is the MEDIAN of --repeats runs (default 3); the minimum
// and maximum are reported too. Nothing here is estimated: every figure printed is measured.
//
// Memory: on Windows the process PEAK working set is read after each benchmark (it is monotonic for the process, so
// it is a "peak so far" figure, not a per-benchmark delta). It is reported as 0 on other platforms.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "opticalnet/failures/FailureState.hpp"
#include "opticalnet/resources/ConnectionManager.hpp"
#include "opticalnet/routing/RoutingEngine.hpp"
#include "opticalnet/simulation/SimulationEngine.hpp"
#include "opticalnet/simulation/SimulationStudy.hpp"
#include "opticalnet/topology/TopologyIo.hpp"
#include "opticalnet/validation/Invariants.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

using namespace opticalnet;
using Clock = std::chrono::steady_clock;

namespace {

double peakWorkingSetMiB() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#endif
    return 0.0;
}

struct Row {
    std::string name;
    std::string workload;
    double medianMs = 0, minMs = 0, maxMs = 0;
    double operations = 0;  // for throughput
    std::string unit;       // what "operations" counts
    double peakMiB = 0;
    std::string note;
    std::uint64_t checksum = 0;
};

// Result fingerprint: every benchmark folds what it computed (paths, metrics) into g_sum, so a run before and after an
// optimization can be compared bit for bit. It is reset before each repetition.
std::uint64_t g_sum = 0;
void mix(std::uint64_t v) {
    g_sum ^= v + 0x9E3779B97F4A7C15ULL + (g_sum << 6) + (g_sum >> 2);
}
void mixDouble(double d) {
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    mix(bits);
}
void mixMetrics(const SimulationMetrics& m) {
    mix(m.totalRequests); mix(m.acceptedRequests); mix(m.rejectedRequests); mix(m.peakAllocatedChannels);
    mix(m.finalAllocatedChannels); mix(m.eventsProcessed); mix(m.totalReleases); mix(m.connectionsAffected);
    mix(m.connectionsRerouted); mix(m.connectionsLost); mix(m.maxHopCount);
    mixDouble(m.averagePathCost); mixDouble(m.averageHopCount); mixDouble(m.averageUtilization);
    mixDouble(m.totalConnectionDowntime);
    for (const auto& [code, n] : m.rejectionsByReason) { mix(static_cast<std::uint64_t>(code)); mix(n); }
}
void mixPath(const Path& p) {
    mixDouble(p.cost());
    for (NodeId n : p.nodes()) mix(n.value());
    for (LinkId l : p.links()) mix(l.value());
}

std::vector<Row> g_rows;
int g_repeats = 3;
bool g_quick = false;
std::string g_only;

template <class T>
T must(Result<T>&& r) {
    if (!r.ok()) {
        std::cerr << "benchmark setup failed: " << r.error().describe() << "\n";
        std::exit(2);
    }
    return std::move(r).value();
}
void mustOk(const Result<void>& r) {
    if (!r.ok()) {
        std::cerr << "benchmark setup failed: " << r.error().describe() << "\n";
        std::exit(2);
    }
}

// Deterministic W x H grid. Node id = y*W + x + 1. Every node links to its right and lower neighbour, with
// deterministic lengths 20..69 km, administrative costs 1..5 and `capacity` channels.
Topology makeGrid(int w, int h, std::uint32_t capacity = 16) {
    Topology t;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto id = static_cast<std::uint32_t>(y * w + x + 1);
            mustOk(t.addNode(must(Node::create(NodeId(id), "n" + std::to_string(id)))));
        }
    std::uint32_t link = 1;
    auto add = [&](int x1, int y1, int x2, int y2) {
        const auto a = static_cast<std::uint32_t>(y1 * w + x1 + 1);
        const auto b = static_cast<std::uint32_t>(y2 * w + x2 + 1);
        const double length = 20.0 + ((x1 * 7 + y1 * 13 + x2 + y2) % 50);
        const double cost = 1.0 + ((x1 + y1) % 5);
        mustOk(t.addLink(must(FiberLink::create(LinkId(link), "l" + std::to_string(link), NodeId(a), NodeId(b), length, 0.2,
                                                capacity, LinkDirection::Bidirectional, cost))));
        ++link;
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (x + 1 < w) add(x, y, x + 1, y);
            if (y + 1 < h) add(x, y, x, y + 1);
        }
    return t;
}

void record(Row row, std::vector<double> samplesMs) {
    std::ranges::sort(samplesMs);
    row.minMs = samplesMs.front();
    row.maxMs = samplesMs.back();
    row.medianMs = samplesMs[samplesMs.size() / 2];
    row.peakMiB = peakWorkingSetMiB();
    g_rows.push_back(std::move(row));
}

// Runs `body` g_repeats times; `body` returns the number of operations it performed (for throughput).
void bench(const std::string& name, const std::string& workload, const std::string& unit, const std::function<double()>& body,
           const std::string& note = {}) {
    if (!g_only.empty() && name.find(g_only) == std::string::npos) return;
    std::vector<double> samples;
    double ops = 0;
    for (int i = 0; i < g_repeats; ++i) {
        g_sum = 0;
        const auto start = Clock::now();
        ops = body();
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    Row row;
    row.name = name;
    row.workload = workload;
    row.operations = ops;
    row.unit = unit;
    row.note = note;
    row.checksum = g_sum;
    record(std::move(row), samples);
    const Row& r = g_rows.back();
    std::printf("%-34s %10.2f ms (min %.2f, max %.2f)", r.name.c_str(), r.medianMs, r.minMs, r.maxMs);
    if (r.operations > 0 && r.medianMs > 0) std::printf("  %12.0f %s/s", r.operations / (r.medianMs / 1000.0), r.unit.c_str());
    std::printf("   peakWS %.0f MiB  checksum %016llx\n", r.peakMiB, static_cast<unsigned long long>(r.checksum));
    std::fflush(stdout);
}

std::vector<std::pair<NodeId, NodeId>> randomPairs(std::uint32_t nodes, int count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<std::pair<NodeId, NodeId>> pairs;
    while (static_cast<int>(pairs.size()) < count) {
        const auto a = static_cast<std::uint32_t>(rng() % nodes) + 1;
        const auto b = static_cast<std::uint32_t>(rng() % nodes) + 1;
        if (a != b) pairs.emplace_back(NodeId(a), NodeId(b));
    }
    return pairs;
}

SimulationConfig simConfig(double endTime, double rate, std::uint64_t seed) {
    SimulationConfig c;
    c.seed = seed;
    c.endTime = endTime;
    c.arrivalRate = rate;
    c.meanLifetime = 8.0;
    c.minCapacityChannels = 1;
    c.maxCapacityChannels = 2;
    c.routing.metric = CostMetric::Distance;
    return c;
}

std::string describeGrid(int w, int h, const Topology& t) {
    return std::to_string(w) + "x" + std::to_string(h) + " grid, " + std::to_string(t.nodeCount()) + " nodes, " +
           std::to_string(t.linkCount()) + " links";
}

}  // namespace

int main(int argc, char** argv) {
    std::string jsonPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--quick") g_quick = true;
        else if (a == "--repeats" && i + 1 < argc) g_repeats = std::max(1, std::atoi(argv[++i]));
        else if (a == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (a == "--only" && i + 1 < argc) g_only = argv[++i];
        else {
            std::cerr << "usage: opticalnet_bench [--quick] [--repeats N] [--json FILE] [--only SUBSTRING]\n";
            return 1;
        }
    }
    const double q = g_quick ? 0.1 : 1.0;  // workload scale
#ifdef NDEBUG
    const char* build = "Release (NDEBUG)";
#else
    const char* build = "Debug (numbers are NOT representative)";
#endif
    std::printf("OpticalNet benchmark | build: %s | hardware threads: %u | repeats: %d | %s\n\n", build,
                std::thread::hardware_concurrency(), g_repeats, g_quick ? "quick workload" : "full workload");

    // ---------------------------------------------------------------- topology
    const int gw = g_quick ? 40 : 100, gh = g_quick ? 40 : 100;
    bench("topology.construct", "grid " + std::to_string(gw) + "x" + std::to_string(gh), "nodes+links", [&] {
        Topology t = makeGrid(gw, gh);
        return static_cast<double>(t.nodeCount() + t.linkCount());
    });

    const Topology big = makeGrid(gw, gh);
    const std::string bigDesc = describeGrid(gw, gh, big);
    std::string json;
    bench("topology.export_json", bigDesc, "links", [&] {
        json = topologyToJson(big, 0);
        return static_cast<double>(big.linkCount());
    });
    bench("topology.load_json", bigDesc + ", " + std::to_string(json.size() / 1024) + " KiB JSON", "links", [&] {
        Topology t = must(loadTopologyFromJson(json));
        return static_cast<double>(t.linkCount());
    });
    bench("topology.connectivity", bigDesc + " (isConnected + isStronglyConnected + components)", "runs", [&] {
        const bool a = big.isConnected();
        const bool b = big.isStronglyConnected();
        const auto c = big.connectedComponents();
        return (a && b && c.size() == 1) ? 3.0 : 0.0;
    });
    bench("topology.validate", bigDesc, "runs", [&] { return big.validate().valid() ? 1.0 : 0.0; });
    bench("topology.neighbors", bigDesc + ", neighbors() of every node", "nodes", [&] {
        std::size_t total = 0;
        for (const auto& [id, node] : big.network().nodes()) total += big.neighbors(id).size();
        return total > 0 ? static_cast<double>(big.nodeCount()) : 0.0;
    });

    // ---------------------------------------------------------------- routing
    const int queries = static_cast<int>(1000 * (g_quick ? 0.2 : 1.0));
    const auto pairs = randomPairs(static_cast<std::uint32_t>(big.nodeCount()), queries, 11);
    RoutingEngine engine;
    auto route = [&](const RoutingConstraints& c) {
        double found = 0;
        for (const auto& [s, d] : pairs) {
            auto r = engine.findPath(big, s, d, c);
            if (r.ok()) { ++found; mixPath(r.value()); } else mix(static_cast<std::uint64_t>(r.error().code) + 1000);
        }
        return found;
    };
    const std::vector<std::pair<CostMetric, const char*>> metrics = {
        {CostMetric::HopCount, "hop_count"}, {CostMetric::Distance, "distance"}, {CostMetric::Administrative, "administrative"}};
    for (const auto& [metric, name] : metrics) {
        RoutingConstraints c;
        c.metric = metric;
        bench(std::string("routing.dijkstra.") + name, bigDesc + ", " + std::to_string(queries) + " random pairs", "queries",
              [&, c] {
                  route(c);
                  return static_cast<double>(queries);
              });
    }
    {
        RoutingConstraints c;
        c.metric = CostMetric::Distance;
        c.maxHops = static_cast<std::uint32_t>(gw + gh);  // generous, but forces the hop-limit check
        bench("routing.constrained.max_hops", bigDesc + ", maxHops=" + std::to_string(*c.maxHops), "queries", [&, c] {
            route(c);
            return static_cast<double>(queries);
        });
        RoutingConstraints tight;
        tight.metric = CostMetric::Distance;
        tight.maxHops = static_cast<std::uint32_t>((gw + gh) * 3 / 4);
        bench("routing.constrained.tight_hops", bigDesc + ", maxHops=" + std::to_string(*tight.maxHops) + " (layered search when binding)",
              "queries", [&, tight] {
                  route(tight);
                  return static_cast<double>(queries);
              });
    }
    {
        RoutingConstraints c;
        c.metric = CostMetric::Distance;
        std::mt19937_64 rng(5);
        for (std::size_t i = 0; i < big.nodeCount() / 20; ++i)  // block ~5% of nodes
            c.blockedNodes.insert(NodeId(static_cast<std::uint32_t>(rng() % big.nodeCount()) + 1));
        c.minLinkCapacityChannels = 8;
        bench("routing.constrained.blocked_nodes", bigDesc + ", " + std::to_string(c.blockedNodes.size()) + " blocked nodes", "queries",
              [&, c] {
                  route(c);
                  return static_cast<double>(queries);
              });
    }

    // ---------------------------------------------------------------- resources
    {
        const Topology grid = makeGrid(30, 30);
        const auto ps = randomPairs(static_cast<std::uint32_t>(grid.nodeCount()), 200, 21);
        std::vector<Path> paths;
        RoutingConstraints c;
        c.metric = CostMetric::HopCount;
        for (const auto& [s, d] : ps) paths.push_back(must(engine.findPath(grid, s, d, c)));
        const int rounds = static_cast<int>(500 * (g_quick ? 0.2 : 1.0));
        bench("resources.allocate_release", describeGrid(30, 30, grid) + ", " + std::to_string(paths.size()) + " paths x " +
                  std::to_string(rounds) + " rounds, demand 1", "alloc+release ops", [&] {
            ResourceManager rm(grid);
            double ops = 0;
            for (int r = 0; r < rounds; ++r)
                for (const Path& p : paths) {
                    mustOk(rm.allocate(p.links(), 1));
                    mustOk(rm.release(p.links(), 1));
                    ops += 2;
                }
            return ops;
        });
        const int n = static_cast<int>(3000 * (g_quick ? 0.2 : 1.0));
        const auto cp = randomPairs(static_cast<std::uint32_t>(grid.nodeCount()), n, 33);
        bench("connections.establish_release", describeGrid(30, 30, grid) + ", " + std::to_string(n) +
                  " establish then release (capacity-aware routing)", "connection ops", [&] {
            ConnectionManager cm(grid);
            RoutingConstraints rc;
            rc.metric = CostMetric::Distance;
            std::uint32_t id = 1;
            std::vector<ConnectionId> ids;
            double ok = 0;
            for (const auto& [s, d] : cp) {
                auto r = cm.establish(ConnectionRequest{ConnectionId(id), s, d, 1, rc});
                if (r.ok()) { ids.push_back(ConnectionId(id)); ++ok; mixPath(r.value().path()); }
                else mix(static_cast<std::uint64_t>(r.error().code) + 1000);
                ++id;
            }
            for (ConnectionId c2 : ids) mustOk(cm.release(c2));
            return static_cast<double>(n) + ok;
        });
    }

    // ---------------------------------------------------------------- simulation
    const int sw = g_quick ? 12 : 20;
    const Topology simTopo = makeGrid(sw, sw, 24);
    const std::string simDesc = describeGrid(sw, sw, simTopo);
    SimulationEngine sim;
    {
        const SimulationConfig cfg = simConfig(g_quick ? 400.0 : 2000.0, 20.0, 1);
        std::uint64_t events = 0, requests = 0;
        bench("simulation.single", simDesc + ", end_time " + std::to_string(static_cast<int>(cfg.endTime)) + ", rate 20/unit, distance routing",
              "events", [&] {
                  auto r = must(sim.run(simTopo, cfg));
                  events = r.metrics.eventsProcessed;
                  requests = r.metrics.totalRequests;
                  mixMetrics(r.metrics);
                  return static_cast<double>(events);
              });
        g_rows.back().workload += " -> " + std::to_string(requests) + " requests, " + std::to_string(events) + " events";
    }
    {
        SimulationConfig cfg = simConfig(g_quick ? 400.0 : 2000.0, 20.0, 1);
        cfg.recordTrace = true;
        std::uint64_t events = 0;
        bench("simulation.single_traced", simDesc + ", same workload with the event trace recorded", "events", [&] {
            auto r = must(sim.run(simTopo, cfg));
            events = r.metrics.eventsProcessed;
            mixMetrics(r.metrics);
            for (const auto& t : r.trace) { mixDouble(t.time); mix(t.connection.value()); mix(t.hops); mix(t.success); }
            return static_cast<double>(events);
        });
    }
    {
        SimulationConfig cfg = simConfig(g_quick ? 400.0 : 2000.0, 20.0, 1);
        cfg.randomFailures = RandomFailureConfig{0.3, 6.0, 0.25};
        std::uint64_t events = 0, affected = 0, failures = 0;
        bench("simulation.failures", simDesc + ", random failures 0.3/unit (25% nodes), reroute policy", "events", [&] {
            auto r = must(sim.run(simTopo, cfg));
            events = r.metrics.eventsProcessed;
            mixMetrics(r.metrics);
            affected = r.metrics.connectionsAffected;
            failures = r.metrics.totalFailureEvents;
            return static_cast<double>(events);
        });
        g_rows.back().workload += " -> " + std::to_string(events) + " events, " + std::to_string(failures) + " failure events, " +
                                  std::to_string(affected) + " connections affected";
    }

    // ---------------------------------------------------------------- studies
    {
        const SimulationConfig cfg = simConfig(g_quick ? 200.0 : 600.0, 20.0, 1);
        const std::uint64_t reps = g_quick ? 8 : 32;
        double singleMs = 0;
        std::vector<std::size_t> workers = {1, 2, 4, 8};
        workers.push_back(0);  // automatic
        for (std::size_t w : workers) {
            StudyConfig sc;
            sc.replications = reps;
            sc.workerCount = w;
            sc.baseSeed = 7;
            std::uint64_t events = 0;
            std::size_t used = 0;
            const std::string name = "study.workers_" + (w == 0 ? std::string("auto") : std::to_string(w));
            bench(name, simDesc + ", " + std::to_string(reps) + " replications of end_time " + std::to_string(static_cast<int>(cfg.endTime)),
                  "events", [&] {
                      auto r = must(SimulationStudy::run(simTopo, cfg, sc));
                      events = 0;
                      for (const auto& rep : r.replications) { events += rep.metrics.eventsProcessed; mixMetrics(rep.metrics); }
                      mixDouble(r.aggregate.aggregateAcceptanceRate); mixDouble(r.aggregate.acceptanceRate.stdDev);
                      used = r.workersUsed;
                      return static_cast<double>(events);
                  });
            if (g_rows.empty() || g_rows.back().name != name) continue;
            Row& row = g_rows.back();
            row.workload += " -> " + std::to_string(events) + " events total, " + std::to_string(used) + " threads created";
            if (w == 1) singleMs = row.medianMs;
            else if (singleMs > 0) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "speedup vs 1 worker: %.2fx", singleMs / row.medianMs);
                row.note = buf;
            }
        }
    }

    // ---------------------------------------------------------------- validation framework
    {
        bench("validation.check_topology", bigDesc, "runs", [&] { return checkTopology(big).passed() ? 1.0 : 0.0; });
        SimulationConfig cfg = simConfig(g_quick ? 200.0 : 600.0, 20.0, 3);
        cfg.recordTrace = true;
        cfg.randomFailures = RandomFailureConfig{0.2, 6.0, 0.25};
        const auto result = must(sim.run(simTopo, cfg));
        bench("validation.check_trace", simDesc + ", trace of " + std::to_string(result.trace.size()) + " entries (failures on)",
              "trace entries", [&] {
                  if (!checkSimulationTrace(simTopo, result).passed()) std::exit(3);
                  return static_cast<double>(result.trace.size());
              });
    }

    // ---------------------------------------------------------------- output
    if (!jsonPath.empty()) {
        std::ofstream out(jsonPath);
        out << "{\n  \"build\": \"" << build << "\",\n  \"hardware_threads\": " << std::thread::hardware_concurrency()
            << ",\n  \"repeats\": " << g_repeats << ",\n  \"quick\": " << (g_quick ? "true" : "false") << ",\n  \"results\": [\n";
        for (std::size_t i = 0; i < g_rows.size(); ++i) {
            const Row& r = g_rows[i];
            out << "    {\"name\": \"" << r.name << "\", \"workload\": \"" << r.workload << "\", \"median_ms\": " << r.medianMs
                << ", \"min_ms\": " << r.minMs << ", \"max_ms\": " << r.maxMs << ", \"operations\": " << r.operations
                << ", \"unit\": \"" << r.unit << "\", \"peak_working_set_mib\": " << r.peakMiB << ", \"note\": \"" << r.note << "\", \"checksum\": \"" << std::hex << r.checksum << std::dec << "\"}"
                << (i + 1 < g_rows.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
        std::printf("\nwrote %s\n", jsonPath.c_str());
    }
    (void)q;
    return 0;
}

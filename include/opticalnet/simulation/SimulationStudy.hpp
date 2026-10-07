#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/simulation/SimulationConfig.hpp"
#include "opticalnet/simulation/SimulationResult.hpp"
#include "opticalnet/simulation/Statistics.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// Phase 6 parallelises INDEPENDENT SIMULATION REPLICATIONS. Each replication is a complete Phase 5 run
// (SimulationEngine::run); an individual run's event loop remains single-threaded.
//
//   Simulation study ─► coordinator ─► worker pool ─┬─ replication 0: own Rng, EventQueue, ConnectionManager,
//                                                   ├─ replication 1: ResourceManager, metrics   (nothing shared
//                                                   └─ replication N-1:                           between them)
//
// Ownership / thread-safety model
//   Topology            shared, IMMUTABLE and read concurrently by all workers. The caller must not modify it
//                       while a study runs (no locks protect it; Topology is not safe for concurrent mutation).
//   SimulationConfig    shared, read-only; each replication works on its own copy with its own seed. If it
//                       contains a RoutingConstraints::linkFilter, that callable is invoked from several
//                       threads at once and must be thread-safe.
//   Rng / EventQueue / ConnectionManager / ResourceManager / metrics
//                       one set per replication, created and destroyed inside that replication's task.
//   Results             each task returns its SimulationResult to the coordinator through a future; the
//                       coordinator alone assembles the study result.
// There is no global or otherwise shared mutable simulation state.
struct StudyConfig {
    // Number of independent replications. Must be >= 1 (a study of nothing is rejected rather than
    // producing meaningless aggregates) and at most kMaxReplications.
    std::uint64_t replications = 1;
    // Worker threads. 0 = std::thread::hardware_concurrency() (1 if the platform reports 0). The pool never
    // gets more threads than there are replications.
    std::size_t workerCount = 0;
    // Replication i runs with seed deriveReplicationSeed(baseSeed, i). SimulationConfig::seed is ignored.
    std::uint64_t baseSeed = 1;

    static constexpr std::uint64_t kMaxReplications = 1'000'000;
};

// Seed of replication `index`: splitmix64(baseSeed + (index + 1) * 0x9E3779B97F4A7C15).
// Depends only on (baseSeed, index) - never on which worker runs the replication or when - and is injective in
// `index` for a fixed base seed (an odd multiplier is a bijection modulo 2^64, as is the splitmix64 mixer), so
// no two replications of a study ever share a seed. Different base seeds give different sequences (their
// seed sets would only overlap if the bases differed by a multiple of the golden-ratio constant).
[[nodiscard]] std::uint64_t deriveReplicationSeed(std::uint64_t baseSeed, std::uint64_t index) noexcept;

// Study-level figures. Replications are the unit: totals add up across replications; "mean" figures give each
// replication equal weight; ratios are computed from the totals (never as a mean of per-replication ratios).
//   totalRequests / totalAccepted / totalRejected / totalReleases   sums over replications
//   rejectionsByReason                                              per-ErrorCode sums
//   aggregateAcceptanceRate = totalAccepted / totalRequests         (0 if totalRequests == 0)
//   aggregateBlockingRate   = totalRejected / totalRequests         (0 if totalRequests == 0)
//   meanActiveConnectionsAtEnd, meanPeakActiveConnections,
//   meanPeakAllocatedChannels, meanFinalAllocatedChannels,
//   meanAverageUtilization                                          arithmetic means over replications
//   averagePathCost, averageHopCount                                mean per ACCEPTED connection across the whole
//                                                                   study, i.e. sum(replication mean * accepted) / totalAccepted
//                                                                   (0 if nothing was accepted)
//   acceptanceRate / blockingRate / averageUtilization              distribution of the per-replication values
//                                                                   (mean, sample std. dev., standard error, 95% CI)
struct StudyAggregate {
    std::uint64_t totalRequests = 0;
    std::uint64_t totalAccepted = 0;
    std::uint64_t totalRejected = 0;
    std::uint64_t totalReleases = 0;
    std::map<ErrorCode, std::uint64_t> rejectionsByReason;
    double aggregateAcceptanceRate = 0.0;
    double aggregateBlockingRate = 0.0;

    double meanActiveConnectionsAtEnd = 0.0;
    double meanPeakActiveConnections = 0.0;
    double meanPeakAllocatedChannels = 0.0;
    double meanFinalAllocatedChannels = 0.0;
    double meanAverageUtilization = 0.0;

    double averagePathCost = 0.0;
    double averageHopCount = 0.0;

    SampleStatistics acceptanceRate;
    SampleStatistics blockingRate;
    SampleStatistics averageUtilization;

    friend bool operator==(const StudyAggregate&, const StudyAggregate&) = default;
};

[[nodiscard]] StudyAggregate aggregate(const std::vector<SimulationResult>& replications);

struct StudyResult {
    std::uint64_t baseSeed = 0;
    std::size_t workersUsed = 0;                 // threads actually created (not part of the simulation outcome)
    std::vector<SimulationResult> replications;  // ordered by replication index, never by completion order
    StudyAggregate aggregate;

    // Equality of the simulation outcome. `workersUsed` is deliberately excluded: results must not depend on it.
    [[nodiscard]] bool sameOutcome(const StudyResult& other) const {
        return baseSeed == other.baseSeed && replications == other.replications && aggregate == other.aggregate;
    }
};

class SimulationStudy {
public:
    // Runs `studyConfig.replications` replications of `config` over `topology`, concurrently.
    // Reproducible: for the same inputs, replication i yields the same SimulationResult whatever the worker
    // count or scheduling (and equals SimulationEngine::run with the derived seed).
    //   InvalidArgument  bad StudyConfig or SimulationConfig (checked before any thread starts)
    //   <error of the lowest-indexed failing replication>, message prefixed "replication <i>: "
    //   InternalError    a replication threw an exception, or worker threads could not be created
    // If any replication fails the whole study fails; the other replications still run to completion and every
    // worker is joined before this function returns (nothing is cancelled, nothing is left running).
    [[nodiscard]] static Result<StudyResult> run(const Topology& topology, const SimulationConfig& config,
                                                 const StudyConfig& studyConfig);

    // Lower-level entry point (and test seam): the same coordinator, but each replication is computed by
    // `replication(index, derivedSeed)`. `replication` is invoked concurrently from several threads, so it
    // must be thread-safe. Same error behaviour as run().
    using ReplicationFunction = std::function<Result<SimulationResult>(std::uint64_t index, std::uint64_t seed)>;
    [[nodiscard]] static Result<StudyResult> runReplications(const StudyConfig& studyConfig, const ReplicationFunction& replication);

    // How many threads a study with these settings would create.
    [[nodiscard]] static std::size_t resolveWorkerCount(std::size_t requested, std::uint64_t replications);
};

}  // namespace opticalnet

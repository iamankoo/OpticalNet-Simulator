#include "opticalnet/simulation/SimulationStudy.hpp"

#include <algorithm>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "opticalnet/concurrency/ThreadPool.hpp"
#include "opticalnet/simulation/SimulationEngine.hpp"

namespace opticalnet {

std::uint64_t deriveReplicationSeed(std::uint64_t baseSeed, std::uint64_t index) noexcept {
    std::uint64_t z = baseSeed + (index + 1) * 0x9E3779B97F4A7C15ULL;  // splitmix64
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::size_t SimulationStudy::resolveWorkerCount(std::size_t requested, std::uint64_t replications) {
    std::size_t workers = requested;
    if (workers == 0) {
        workers = std::thread::hardware_concurrency();
        if (workers == 0) workers = 1;  // the platform could not tell
    }
    if (replications < workers) workers = static_cast<std::size_t>(replications);  // never more threads than work
    return std::max<std::size_t>(workers, 1);
}

StudyAggregate aggregate(const std::vector<SimulationResult>& replications) {
    StudyAggregate a;
    if (replications.empty()) return a;

    double activeAtEnd = 0, peakActive = 0, peakAllocated = 0, finalAllocated = 0, utilization = 0;
    double weightedCost = 0.0, weightedHops = 0.0;
    std::vector<double> acceptance, blocking, utilizationValues;
    for (const SimulationResult& r : replications) {
        const SimulationMetrics& m = r.metrics;
        a.totalRequests += m.totalRequests;
        a.totalAccepted += m.acceptedRequests;
        a.totalRejected += m.rejectedRequests;
        a.totalReleases += m.totalReleases;
        for (const auto& [reason, count] : m.rejectionsByReason) a.rejectionsByReason[reason] += count;
        activeAtEnd += static_cast<double>(m.activeConnectionsAtEnd);
        peakActive += static_cast<double>(m.peakActiveConnections);
        peakAllocated += static_cast<double>(m.peakAllocatedChannels);
        finalAllocated += static_cast<double>(m.finalAllocatedChannels);
        utilization += m.averageUtilization;
        weightedCost += m.averagePathCost * static_cast<double>(m.acceptedRequests);
        weightedHops += m.averageHopCount * static_cast<double>(m.acceptedRequests);
        acceptance.push_back(m.acceptanceRate);
        blocking.push_back(m.blockingRate);
        utilizationValues.push_back(m.averageUtilization);
    }

    const double n = static_cast<double>(replications.size());
    if (a.totalRequests > 0) {
        a.aggregateAcceptanceRate = static_cast<double>(a.totalAccepted) / static_cast<double>(a.totalRequests);
        a.aggregateBlockingRate = static_cast<double>(a.totalRejected) / static_cast<double>(a.totalRequests);
    }
    a.meanActiveConnectionsAtEnd = activeAtEnd / n;
    a.meanPeakActiveConnections = peakActive / n;
    a.meanPeakAllocatedChannels = peakAllocated / n;
    a.meanFinalAllocatedChannels = finalAllocated / n;
    a.meanAverageUtilization = utilization / n;
    if (a.totalAccepted > 0) {
        a.averagePathCost = weightedCost / static_cast<double>(a.totalAccepted);
        a.averageHopCount = weightedHops / static_cast<double>(a.totalAccepted);
    }
    a.acceptanceRate = summarize(acceptance);
    a.blockingRate = summarize(blocking);
    a.averageUtilization = summarize(utilizationValues);
    return a;
}

Result<StudyResult> SimulationStudy::run(const Topology& topology, const SimulationConfig& config,
                                         const StudyConfig& studyConfig) {
    if (auto valid = config.validate(); !valid.ok()) return valid.error();
    // Each task copies the shared read-only config, sets its own seed and runs a private engine; the topology
    // is only read.
    return runReplications(studyConfig, [&topology, &config](std::uint64_t, std::uint64_t seed) -> Result<SimulationResult> {
        SimulationConfig mine = config;
        mine.seed = seed;
        return SimulationEngine{}.run(topology, mine);
    });
}

Result<StudyResult> SimulationStudy::runReplications(const StudyConfig& studyConfig, const ReplicationFunction& replication) {
    if (studyConfig.replications == 0)
        return Error{ErrorCode::InvalidArgument, "a study needs at least one replication"};
    if (studyConfig.replications > StudyConfig::kMaxReplications)
        return Error{ErrorCode::InvalidArgument,
                     "a study is limited to " + std::to_string(StudyConfig::kMaxReplications) + " replications"};

    const std::size_t workers = resolveWorkerCount(studyConfig.workerCount, studyConfig.replications);
    const std::size_t count = static_cast<std::size_t>(studyConfig.replications);

    std::unique_ptr<ThreadPool> pool;
    try {
        pool = std::make_unique<ThreadPool>(workers);
    } catch (const std::exception& e) {
        return Error{ErrorCode::InternalError, std::string("could not start worker threads: ") + e.what()};
    }

    // Dispatch: replication i gets the seed derived from (baseSeed, i) here, on the coordinating thread, so the
    // seed cannot depend on scheduling. The futures vector keeps submission (= index) order.
    std::vector<std::future<Result<SimulationResult>>> futures;
    futures.reserve(count);
    try {
        for (std::uint64_t i = 0; i < studyConfig.replications; ++i) {
            const std::uint64_t seed = deriveReplicationSeed(studyConfig.baseSeed, i);
            futures.push_back(pool->submit([&replication, i, seed] { return replication(i, seed); }));
        }
    } catch (const std::exception& e) {
        pool->shutdown();  // lets already-queued tasks finish while `replication` is still alive
        return Error{ErrorCode::InternalError, std::string("could not schedule replications: ") + e.what()};
    }

    // Collect in index order, waiting for EVERY replication even after a failure so that no task is still running
    // (and referencing caller data) when this function returns.
    std::vector<SimulationResult> results;
    results.reserve(count);
    std::optional<Error> firstError;  // lowest-indexed failure, so the reported error is deterministic
    const auto fail = [&firstError](std::size_t index, ErrorCode code, const std::string& message) {
        if (!firstError) firstError = Error{code, "replication " + std::to_string(index) + ": " + message};
    };
    for (std::size_t i = 0; i < count; ++i) {
        try {
            Result<SimulationResult> outcome = futures[i].get();
            if (outcome.ok()) {
                results.push_back(std::move(outcome).value());
            } else {
                fail(i, outcome.error().code, outcome.error().message);
            }
        } catch (const std::exception& e) {
            fail(i, ErrorCode::InternalError, std::string("threw an exception: ") + e.what());
        } catch (...) {
            fail(i, ErrorCode::InternalError, "threw an unknown exception");
        }
    }
    pool->shutdown();  // joins every worker
    if (firstError) return *firstError;

    StudyResult study;
    study.baseSeed = studyConfig.baseSeed;
    study.workersUsed = workers;
    study.replications = std::move(results);
    study.aggregate = opticalnet::aggregate(study.replications);
    return study;
}

}  // namespace opticalnet

#pragma once

#include <cstdint>
#include <vector>

#include "opticalnet/failures/FailureSchedule.hpp"
#include "opticalnet/simulation/SimulationEvent.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// Expands a RandomFailureConfig into a concrete, deterministic failure schedule over [0, endTime].
//
// Failures form a Poisson process; each picks a uniformly random resource that is healthy at that moment
// (a node with probability nodeFailureProbability, else a link) and recovers after an exponential repair time
// (a recovery later than endTime is not generated). The schedule depends only on (topology ids, config,
// endTime, seed): never on traffic, so it is the same on every worker thread, and it comes from its own RNG
// stream so switching failures on or off does not change the request workload. It is always valid
// (validateFailureSchedule accepts it) and only refers to existing ids. Entries are in generation order.
[[nodiscard]] std::vector<ScriptedFailure> generateRandomFailures(const Topology& topology, const RandomFailureConfig& config,
                                                                  SimTime endTime, std::uint64_t seed);

}  // namespace opticalnet

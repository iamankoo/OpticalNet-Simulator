#pragma once

#include <span>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/resources/Connection.hpp"
#include "opticalnet/simulation/RequestGenerator.hpp"
#include "opticalnet/simulation/SimulationConfig.hpp"
#include "opticalnet/simulation/SimulationResult.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// A request placed on the timeline explicitly (for hand-computed scenarios and tests).
struct ScriptedArrival {
    SimTime time;          // >= 0
    ConnectionRequest request;
    SimTime lifetime;      // > 0; the release is scheduled at time + lifetime if the request is accepted
};

// Discrete-event simulation of connection requests over a topology.
//
//   SimulationConfig -> SimulationEngine -> EventQueue / RequestGenerator / Rng
//                                        -> ConnectionManager (routing + resources) -> Metrics
//
// Virtual time: the clock jumps straight from event to event. Nothing sleeps, waits or reads wall-clock
// time, so a run covering a million time units takes as long as its events need to be processed.
//
// Event handling
//   ConnectionArrival: ConnectionManager::establish. Accepted -> a ConnectionRelease is scheduled at
//                      arrival + lifetime. Rejected -> recorded with its reason.
//   ConnectionRelease: ConnectionManager::release of exactly the stored connection.
//
// Each run builds its own ConnectionManager, leaving the topology untouched (it only supplies the static
// structure), so the engine is stateless: runs are independent and repeatable.
//
// Ending a run: events are processed in order while their time is <= endTime; arrivals are generated only
// up to endTime (and maxRequests). Releases beyond endTime are not processed, so those connections are
// still active at the end.
//
// At the end the engine verifies that the ConnectionManager/ResourceManager are consistent and that its
// own channel bookkeeping matches the ResourceManager; a mismatch is reported as InconsistentState.
//
// Phase 5 is intentionally single-threaded; Phase 6 introduces concurrent simulation workers.
class SimulationEngine {
public:
    // Generated workload. Errors: InvalidArgument (bad config).
    [[nodiscard]] Result<SimulationResult> run(const Topology& topology, const SimulationConfig& config) const;

    // Explicit workload. Uses config.endTime and config.recordTrace only (seed, rates and request
    // parameters are ignored; each ScriptedArrival carries its own request). Arrivals with time > endTime
    // are ignored. Arrivals at the same time are processed in span order.
    //   InvalidArgument: bad endTime, or an arrival with negative/NaN time or a non-positive lifetime
    [[nodiscard]] Result<SimulationResult> runScript(const Topology& topology, std::span<const ScriptedArrival> arrivals,
                                                     const SimulationConfig& config = {}) const;
};

}  // namespace opticalnet

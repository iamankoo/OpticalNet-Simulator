#pragma once

#include "opticalnet/failures/FailureState.hpp"
#include "opticalnet/resources/ConnectionManager.hpp"
#include "opticalnet/routing/Path.hpp"
#include "opticalnet/routing/RoutingConstraints.hpp"
#include "opticalnet/simulation/SimulationResult.hpp"
#include "opticalnet/topology/Topology.hpp"
#include "opticalnet/validation/ValidationResult.hpp"

namespace opticalnet {

// Independent invariant checkers. Each recomputes what it checks from raw data (the network's element lists, a path's
// links, a simulation trace, ...) with its own straightforward code, rather than asking the production component
// whether it considers itself correct. They never modify anything.

// ---- topology -------------------------------------------------------------------------------------------------
// Referenced nodes/links/equipment exist, ids are unique and match their map keys, no self-links, the adjacency
// indexes equal an independent recomputation from the link list (sets, ordering by LinkId, directed vs.
// bidirectional rules, degree), and, if `checkPorts`, no node has more incident links than its switch has ports.
[[nodiscard]] ValidationResult checkTopology(const Topology& topology, bool checkPorts = true);

// ---- failures -------------------------------------------------------------------------------------------------
// Failed ids exist in the topology; a link is unusable exactly when it, or an end node, is failed.
[[nodiscard]] ValidationResult checkFailureState(const FailureState& failures, const Topology& topology);

// ---- routing --------------------------------------------------------------------------------------------------
struct PathExpectation {
    NodeId source;
    NodeId destination;
    CostMetric metric = CostMetric::Administrative;
    const RoutingConstraints* constraints = nullptr;  // if set: hop/cost limits, blocked elements, capacity, filter are checked
    const FailureState* failures = nullptr;           // if set: no failed link or node may appear
};

// The path starts and ends where requested, is a simple chain of existing links each traversable in the direction used,
// its cost equals the sum of the selected metric over its links, hopCount == number of links, and it satisfies the
// constraints and avoids failed resources (when given). Parallel-link identity is checked via the LinkIds.
[[nodiscard]] ValidationResult checkPath(const Topology& topology, const Path& path, const PathExpectation& expectation);

// ---- resources ------------------------------------------------------------------------------------------------
// For every link 0 <= allocated <= capacity and allocated + available == capacity; totalAllocated equals the sum of
// per-link allocations, totalCapacity the sum of link capacities, totalAvailable == totalCapacity - totalAllocated.
[[nodiscard]] ValidationResult checkResources(const ResourceManager& resources, const Topology& topology);

// ---- connections ----------------------------------------------------------------------------------------------
// Every active connection has a positive demand, a valid feasible path from its request's source to its destination
// (and avoids failed resources if `failures` is given); a shadow ledger summing the demands of all active connections
// per link equals the ResourceManager's allocation on every link (so nothing is leaked and nothing is missing).
[[nodiscard]] ValidationResult checkConnections(const ConnectionManager& manager, const Topology& topology,
                                                const FailureState* failures = nullptr);

// ---- simulation -----------------------------------------------------------------------------------------------
// Replays SimulationResult::trace (which must have been recorded) with an independent ledger and checks that the
// result's metrics agree: counts, rejection reasons, failure/recovery/affected/rerouted/lost counts, peak and final
// allocation, active connections, hop statistics, time-weighted utilization, rates; that time never goes backwards;
// that every release/reroute/loss refers to a connection that was active; and that capacity is never exceeded.
[[nodiscard]] ValidationResult checkSimulationTrace(const Topology& topology, const SimulationResult& result);

}  // namespace opticalnet

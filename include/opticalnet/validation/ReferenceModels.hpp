#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

#include "opticalnet/failures/FailureState.hpp"
#include "opticalnet/routing/RoutingConstraints.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// Reference models: deliberately simple, obviously-correct implementations used as oracles for the production code.

// ---- exhaustive routing oracle --------------------------------------------------------------------------------
// Result of enumerating EVERY simple path (over individual links, so parallel links count) that satisfies the constraints.
struct ReferenceRoute {
    bool found = false;
    double cost = 0.0;         // minimum cost under the constraints' metric
    std::size_t hops = 0;      // fewest hops among the minimum-cost paths
    std::size_t pathsConsidered = 0;
};

// Brute force: depth-first search over the network's LINK LIST (not the topology's adjacency index) with its own
// direction rule, cost formula and constraint checks. Exponential: only for small graphs (a handful of nodes/links).
// Honours maxHops, maxCost, blocked nodes/links, minimum link capacity, the link filter and the failure overlay.
// `source == destination` yields the trivial path (found, cost 0, 0 hops) unless the node is blocked or failed.
[[nodiscard]] ReferenceRoute exhaustiveRoute(const Topology& topology, NodeId source, NodeId destination,
                                             const RoutingConstraints& constraints, const FailureState* failures = nullptr);

// Is there any path at all, ignoring every constraint but link direction (and optionally failures)? Used to tell
// "NoRoute" (unreachable) from "NoFeasibleRoute" (reachable, but not under the constraints).
[[nodiscard]] bool reachableByAnyPath(const Topology& topology, NodeId source, NodeId destination);

// ---- shadow ledger --------------------------------------------------------------------------------------------
// An independent model of per-link channel allocation: a plain map of counters checked against link capacities.
// All-or-nothing like the production ResourceManager, but written separately.
class ShadowLedger {
public:
    explicit ShadowLedger(const Topology& topology) : topology_(&topology) {}

    // Reserve `demand` on every link of `links` (a repeated link counts repeatedly). False, changing nothing, if
    // demand is 0, `links` is empty, a link is unknown, or any link would exceed its capacity.
    bool allocate(std::span<const LinkId> links, std::uint32_t demand);
    // False, changing nothing, if demand is 0, `links` is empty, or any link holds less than requested.
    bool release(std::span<const LinkId> links, std::uint32_t demand);

    [[nodiscard]] std::uint64_t allocated(LinkId link) const;
    [[nodiscard]] std::uint64_t totalAllocated() const;
    [[nodiscard]] const std::map<LinkId, std::uint64_t>& counters() const noexcept { return counters_; }

private:
    const Topology* topology_;
    std::map<LinkId, std::uint64_t> counters_;
};

}  // namespace opticalnet

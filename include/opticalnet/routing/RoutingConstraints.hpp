#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_set>

#include "opticalnet/core/FiberLink.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/topology/LinkCost.hpp"

namespace opticalnet {

// What the caller asks of a route. Routing only answers "is this path feasible?";
// reserving capacity is the resource manager's job (Phase 4), so nothing here
// changes any state.
//
// Constraints take part in the search (violating edges are never explored), they are
// not checked after the fact.
struct RoutingConstraints {
    // Edge weight to minimise. Default: operator-assigned administrative cost.
    CostMetric metric = CostMetric::Administrative;

    // Upper bound on total path cost, in the units of `metric`. Because the optical
    // reach of a transceiver is a distance limit, reach is expressed as
    // {metric = Distance, maxCost = transceiver.reachKm()}.
    std::optional<double> maxCost;

    // Upper bound on the number of links. Respected exactly: a costlier path with few
    // hops is found even when the cheapest path has too many.
    std::optional<std::uint32_t> maxHops;

    // Links whose configured capacity (FiberLink::capacityChannels) is below this are
    // not used. This is *static* capacity; free capacity is dynamic state (see linkFilter).
    std::uint32_t minLinkCapacityChannels = 0;

    // Nodes / links that must not be used (e.g. maintenance windows, protection paths).
    // A blocked source or destination makes the request infeasible.
    std::unordered_set<NodeId> blockedNodes;
    std::unordered_set<LinkId> blockedLinks;

    // Optional extra per-link veto. This is the seam for Phase 4: the resource manager
    // can supply "link has enough free capacity" without routing owning or copying any
    // resource state. Return false to exclude the link. Must be deterministic and must
    // not outlive the data it reads.
    std::function<bool(const FiberLink&)> linkFilter;

    // True if no restriction other than the metric is set.
    [[nodiscard]] bool isUnconstrained() const noexcept {
        return !maxCost && !maxHops && minLinkCapacityChannels == 0 && blockedNodes.empty() &&
               blockedLinks.empty() && !linkFilter;
    }
};

}  // namespace opticalnet

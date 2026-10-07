#pragma once

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/routing/Path.hpp"
#include "opticalnet/routing/RoutingConstraints.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// A path-finding strategy. Implementations are stateless with respect to the topology:
// they read it, never own or modify it, so one instance can serve many queries (and,
// being const and stateless, many threads).
//
// Failure reporting (all via Result, none throw):
//   NotFound           - source or destination is not in the topology
//   InvalidArgument    - malformed constraints (e.g. negative or NaN maxCost)
//   NoRoute            - the destination is unreachable even without constraints
//   NoFeasibleRoute    - a route exists, but none satisfies the constraints
class IRoutingAlgorithm {
public:
    virtual ~IRoutingAlgorithm() = default;

    [[nodiscard]] virtual Result<Path> findPath(const Topology& topology, NodeId source, NodeId destination,
                                                const RoutingConstraints& constraints) const = 0;

protected:
    IRoutingAlgorithm() = default;
    IRoutingAlgorithm(const IRoutingAlgorithm&) = default;
    IRoutingAlgorithm& operator=(const IRoutingAlgorithm&) = default;
};

}  // namespace opticalnet

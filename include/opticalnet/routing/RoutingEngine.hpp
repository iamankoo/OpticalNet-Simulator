#pragma once

#include <memory>

#include "opticalnet/routing/IRoutingAlgorithm.hpp"

namespace opticalnet {

// Entry point for route computation. Holds a routing algorithm (Dijkstra by default)
// but never the topology: the caller passes the Topology on every call, so the engine
// cannot go stale and ownership stays with the topology layer.
//
// The engine finds paths only. It reserves nothing; allocating capacity along the
// returned Path is the resource manager's job (Phase 4).
class RoutingEngine {
public:
    RoutingEngine();  // Dijkstra
    explicit RoutingEngine(std::unique_ptr<IRoutingAlgorithm> algorithm);

    [[nodiscard]] Result<Path> findPath(const Topology& topology, NodeId source, NodeId destination,
                                        const RoutingConstraints& constraints = {}) const;

private:
    std::unique_ptr<IRoutingAlgorithm> algorithm_;
};

}  // namespace opticalnet

#include "opticalnet/routing/RoutingEngine.hpp"

#include <stdexcept>

#include "opticalnet/routing/DijkstraRouter.hpp"

namespace opticalnet {

RoutingEngine::RoutingEngine() : algorithm_(std::make_unique<DijkstraRouter>()) {}

RoutingEngine::RoutingEngine(std::unique_ptr<IRoutingAlgorithm> algorithm) : algorithm_(std::move(algorithm)) {
    if (!algorithm_) throw std::invalid_argument("RoutingEngine requires a routing algorithm");
}

Result<Path> RoutingEngine::findPath(const Topology& topology, NodeId source, NodeId destination,
                                     const RoutingConstraints& constraints) const {
    return algorithm_->findPath(topology, source, destination, constraints);
}

}  // namespace opticalnet

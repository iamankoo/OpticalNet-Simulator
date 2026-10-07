#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "opticalnet/resources/Connection.hpp"
#include "opticalnet/simulation/Rng.hpp"
#include "opticalnet/simulation/SimulationConfig.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// One generated request together with its arrival time and the lifetime it will have if accepted.
struct GeneratedRequest {
    SimTime arrival;
    ConnectionRequest request;
    SimTime lifetime;  // > 0
};

// Produces the workload of a run: a deterministic (for a given seed) sequence of requests with strictly
// increasing arrival times. It is independent of the network's state, and testable on its own.
//
// Draw order per request: gap, source, destination, capacity, lifetime (Fixed timings draw nothing).
// Connection ids are 1, 2, 3, ... in generation order.
class RequestGenerator {
public:
    // Validates the config. Only the topology's node ids are copied, so the generator does not keep
    // a reference to the topology. A topology with fewer than two nodes simply yields no requests.
    [[nodiscard]] static Result<RequestGenerator> create(const Topology& topology, const SimulationConfig& config);

    // The next request, or nullopt when generation is over: arrivalRate is 0, fewer than two nodes,
    // maxRequests reached, or the next arrival would be after endTime. Once nullopt, always nullopt.
    [[nodiscard]] std::optional<GeneratedRequest> next();

    [[nodiscard]] std::uint64_t generated() const noexcept { return generated_; }
    // True if generation stopped because (or is stopped by) the maxRequests cap.
    [[nodiscard]] bool limitReached() const noexcept {
        return config_.maxRequests && generated_ >= *config_.maxRequests;
    }

private:
    RequestGenerator(std::vector<NodeId> nodes, const SimulationConfig& config)
        : nodes_(std::move(nodes)), config_(config), rng_(config.seed) {}

    std::vector<NodeId> nodes_;  // ascending by id
    SimulationConfig config_;
    Rng rng_;
    SimTime clock_ = 0.0;
    std::uint64_t generated_ = 0;
    bool exhausted_ = false;
};

}  // namespace opticalnet

#include "opticalnet/simulation/RequestGenerator.hpp"

#include <cmath>
#include <limits>

namespace opticalnet {

Result<RequestGenerator> RequestGenerator::create(const Topology& topology, const SimulationConfig& config) {
    if (auto valid = config.validate(); !valid.ok()) return valid.error();
    std::vector<NodeId> nodes;
    nodes.reserve(topology.nodeCount());
    for (const auto& entry : topology.network().nodes()) nodes.push_back(entry.first);  // ascending by id
    return RequestGenerator(std::move(nodes), config);
}

std::optional<GeneratedRequest> RequestGenerator::next() {
    if (exhausted_) return std::nullopt;
    if (config_.arrivalRate == 0.0 || nodes_.size() < 2 || limitReached()) {
        exhausted_ = true;
        return std::nullopt;
    }

    // Arrival time: strictly after the previous one.
    const double meanGap = 1.0 / config_.arrivalRate;
    const double gap = config_.arrivalTiming == Timing::Fixed ? meanGap : rng_.exponential(meanGap);
    double arrival = clock_ + gap;
    if (!(arrival > clock_)) arrival = std::nextafter(clock_, std::numeric_limits<double>::infinity());  // gap 0 or absorbed
    if (!std::isfinite(arrival) || arrival > config_.endTime) {
        exhausted_ = true;
        return std::nullopt;
    }
    clock_ = arrival;

    // Endpoints: uniform source, then uniform destination among the *other* nodes, so source != destination.
    const std::size_t source = rng_.uniformIndex(nodes_.size());
    std::size_t destination = rng_.uniformIndex(nodes_.size() - 1);
    if (destination >= source) ++destination;

    const std::uint32_t demand = rng_.uniformInt(config_.minCapacityChannels, config_.maxCapacityChannels);
    double lifetime = config_.lifetimeTiming == Timing::Fixed ? config_.meanLifetime : rng_.exponential(config_.meanLifetime);
    if (!(lifetime > 0.0)) lifetime = std::numeric_limits<double>::min();

    ++generated_;
    return GeneratedRequest{
        arrival, ConnectionRequest{ConnectionId{static_cast<std::uint32_t>(generated_)}, nodes_[source], nodes_[destination], demand, config_.routing},
        lifetime};
}

}  // namespace opticalnet

#pragma once

#include <cstdint>
#include <optional>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/routing/RoutingConstraints.hpp"
#include "opticalnet/simulation/SimulationEvent.hpp"

namespace opticalnet {

// How a time quantity is drawn.
enum class Timing {
    Exponential,  // exponentially distributed with the stated mean (Poisson arrivals / memoryless lifetimes)
    Fixed,        // exactly the stated mean every time (no randomness; for deterministic scenarios)
};

// Parameters of one simulation run (the topology is passed to the engine separately).
//
// Arrivals:  a request arrives every `1 / arrivalRate` time units on average. With Exponential timing
//            the gaps are exponential with mean 1/arrivalRate (a Poisson process of rate arrivalRate).
//            arrivalRate == 0 means no requests at all.
// Lifetimes: a connection lives `meanLifetime` time units on average (Exponential: mean; Fixed: exact).
// Requests:  source and destination are drawn uniformly from the topology's nodes and are always
//            different (self-connections are never generated). Demand is uniform in
//            [minCapacityChannels, maxCapacityChannels].
// Workload and network state are independent: every random quantity of a request (gap, endpoints,
// demand, lifetime) is drawn when the request is generated, whether or not it is later accepted.
struct SimulationConfig {
    std::uint64_t seed = 1;

    // Horizon: arrivals are generated only at times <= endTime and only events with time <= endTime are
    // processed. Connections whose release lies beyond the horizon are still active at the end.
    SimTime endTime = 100.0;

    // Optional cap on the number of requests generated. If the cap is reached and every scheduled
    // release has happened before endTime, the run ends at the time of the last event (see
    // SimulationMetrics::duration); otherwise it ends at endTime. maxRequests == 0 generates nothing.
    std::optional<std::uint64_t> maxRequests;

    double arrivalRate = 1.0;  // requests per time unit, >= 0
    Timing arrivalTiming = Timing::Exponential;
    double meanLifetime = 10.0;  // time units, > 0
    Timing lifetimeTiming = Timing::Exponential;

    std::uint32_t minCapacityChannels = 1;
    std::uint32_t maxCapacityChannels = 1;

    // Applied to every generated request (metric, hop/cost limits, blocked elements, ...).
    RoutingConstraints routing;

    // Keep a per-event trace in the result (memory grows with the number of events).
    bool recordTrace = false;

    // InvalidArgument: endTime negative/NaN/infinite, arrivalRate negative/NaN/infinite,
    // meanLifetime not positive-finite, capacity range empty or starting at 0.
    [[nodiscard]] Result<void> validate() const;
};

}  // namespace opticalnet

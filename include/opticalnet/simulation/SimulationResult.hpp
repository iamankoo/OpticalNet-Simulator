#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/simulation/SimulationEvent.hpp"

namespace opticalnet {

// Plain data, comparable with ==, and free of any dependency on the API layer, so it can be
// serialised later. A run is deterministic: equal inputs and seed give an == result.
//
// Definitions (R = total requests, "channels" = capacity units; a connection of demand d over h links
// holds d*h channel-links; "capacity" = sum of every link's configured channels):
//   acceptanceRate     = acceptedRequests / R                      (0 if R == 0)
//   blockingRate       = rejectedRequests / R                      (0 if R == 0)
//   averageUtilization = (integral of allocated(t) dt over [0, duration])
//                        / (totalCapacityChannels * duration)      (0 if either factor is 0)
//                        i.e. the TIME-weighted fraction of the network's capacity that is in use.
//   peakAllocatedChannels = maximum of allocated(t), observed after each processed event
//   averagePathCost / averageHopCount = mean over ACCEPTED requests (0 if none); the cost is expressed
//                        in the metric each request was routed with
struct SimulationMetrics {
    // Requests
    std::uint64_t totalRequests = 0;
    std::uint64_t acceptedRequests = 0;
    std::uint64_t rejectedRequests = 0;
    std::map<ErrorCode, std::uint64_t> rejectionsByReason;  // e.g. InsufficientCapacity, NoRoute
    double acceptanceRate = 0.0;
    double blockingRate = 0.0;

    // Resources
    std::uint64_t totalCapacityChannels = 0;
    std::uint64_t peakAllocatedChannels = 0;
    double averageUtilization = 0.0;
    std::uint64_t finalAllocatedChannels = 0;
    std::uint64_t finalAvailableChannels = 0;

    // Routing (accepted requests)
    double averagePathCost = 0.0;
    double averageHopCount = 0.0;
    std::size_t maxHopCount = 0;

    // Connections
    std::uint64_t activeConnectionsAtEnd = 0;
    std::uint64_t peakActiveConnections = 0;
    std::uint64_t totalReleases = 0;

    // Failures and recovery (all 0 when no failure is scheduled). A connection is "affected" when a failure hits a
    // resource on its current path; it is then either rerouted (kept, same id) or lost.
    //   totalFailureEvents      = linkFailures + nodeFailures
    //   recoverySuccessRate     = connectionsRerouted / connectionsAffected   (0 if nothing was affected)
    //   totalConnectionDowntime = sum over LOST connections of (time the connection would have ended, capped at the
    //                             end of the run) - (time it was lost). Rerouting completes within the failure event
    //                             (no restoration delay is modelled), so rerouted connections add 0.
    // Path statistics above (averagePathCost, averageHopCount, maxHopCount) describe each connection's FIRST path.
    std::uint64_t totalFailureEvents = 0;
    std::uint64_t linkFailures = 0;
    std::uint64_t nodeFailures = 0;
    std::uint64_t linkRecoveries = 0;
    std::uint64_t nodeRecoveries = 0;
    std::uint64_t connectionsAffected = 0;
    std::uint64_t connectionsRerouted = 0;
    std::uint64_t connectionsLost = 0;
    double recoverySuccessRate = 0.0;
    double totalConnectionDowntime = 0.0;
    std::uint64_t failedLinksAtEnd = 0;
    std::uint64_t failedNodesAtEnd = 0;

    // Run
    std::uint64_t eventsProcessed = 0;
    // Simulated length of the observation window: endTime, except that a run stopped by maxRequests with
    // all releases done ends at its last event.
    SimTime duration = 0.0;

    friend bool operator==(const SimulationMetrics&, const SimulationMetrics&) = default;
};

// What a trace entry records. Event: the processed event itself (see `type`). The others describe the
// consequences of a failure event, in this order for each affected connection (ascending id):
//   Affected -> Rerouted (success, `hops` = new path length) or Lost (success = false, `reason` = why the reroute
//   failed, absent under NoRecovery). For these, `type` is the causing failure event and `resource` the failed id.
enum class TraceKind { Event, Affected, Rerouted, Lost };

// One entry of the optional trace (only recorded when SimulationConfig::recordTrace is set). For link/node
// failure and recovery events `connection`, `source`, `destination` and `capacityChannels` are 0 and `resource`
// is the LinkId/NodeId value.
struct TraceEntry {
    SimTime time;
    EventType type;
    ConnectionId connection;
    NodeId source;
    NodeId destination;
    std::uint32_t capacityChannels;
    bool success;                      // arrival: accepted; release: always true
    std::optional<ErrorCode> reason;   // arrival rejected: why
    std::size_t hops;                  // links of the established path (0 for a rejection)
    std::uint32_t resource = 0;        // failed/recovered LinkId or NodeId value (failure events and their consequences)
    TraceKind kind = TraceKind::Event;

    friend bool operator==(const TraceEntry&, const TraceEntry&) = default;
};

struct SimulationResult {
    std::uint64_t seed = 0;  // 0 for scripted runs
    SimulationMetrics metrics;
    std::vector<TraceEntry> trace;

    friend bool operator==(const SimulationResult&, const SimulationResult&) = default;
};

}  // namespace opticalnet

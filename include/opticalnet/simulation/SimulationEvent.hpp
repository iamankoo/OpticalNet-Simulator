#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"

namespace opticalnet {

// Simulated (virtual) time, in abstract time units. It is just a number that jumps from event to
// event; nothing in the simulator ever sleeps or reads a wall clock.
using SimTime = double;

// The numeric value is the priority among events with the SAME timestamp (lower is processed first):
//
//   recoveries -> failures -> connection releases -> connection arrivals
//
// Network-state changes come first, so releases and arrivals at that instant see the new state. Recoveries
// precede failures so that a reroute triggered by a failure at time t can already use what recovered at t, and
// so that a resource recovering and failing again at the same instant ends up failed. Releases precede
// arrivals (Phase 5 rule) so that capacity freed at time t is usable by a request arriving at t.
enum class EventType : std::uint8_t {
    LinkRecovery = 0,
    NodeRecovery = 1,
    LinkFailure = 2,
    NodeFailure = 3,
    ConnectionRelease = 4,
    ConnectionArrival = 5,
};

[[nodiscard]] bool isFailureEvent(EventType type) noexcept;  // any of the four link/node failure or recovery events

struct SimulationEvent {
    SimTime time;
    EventType type;
    ConnectionId connection;   // the request (arrival) or the established connection (release); 0 for failure events
    std::uint64_t sequence;    // creation order, assigned by the EventQueue
    std::uint32_t resource = 0;  // LinkId or NodeId value for failure/recovery events; unused otherwise
};

// Time-ordered queue of events. Total, deterministic processing order:
//   1. earlier timestamp first
//   2. at equal timestamps, by EventType (recoveries, failures, releases, arrivals; see EventType)
//   3. at equal timestamp and type, by sequence number (the order in which events were scheduled)
class EventQueue {
public:
    // Schedules an event and returns its sequence number.
    //   InvalidArgument: `time` is negative, NaN or infinite
    [[nodiscard]] Result<std::uint64_t> schedule(SimTime time, EventType type, ConnectionId connection);
    // Schedules a link/node failure or recovery event for the given LinkId/NodeId value.
    [[nodiscard]] Result<std::uint64_t> scheduleResource(SimTime time, EventType type, std::uint32_t resource);

    [[nodiscard]] bool empty() const noexcept { return queue_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return queue_.size(); }

    // Next event to process. Precondition: !empty() (otherwise std::logic_error is thrown).
    [[nodiscard]] const SimulationEvent& peek() const;
    // Removes and returns the next event. Precondition: !empty().
    SimulationEvent pop();

private:
    struct Later {  // "a is processed after b"
        bool operator()(const SimulationEvent& a, const SimulationEvent& b) const noexcept {
            if (a.time != b.time) return a.time > b.time;
            if (a.type != b.type) return a.type > b.type;
            return a.sequence > b.sequence;
        }
    };

    std::priority_queue<SimulationEvent, std::vector<SimulationEvent>, Later> queue_;
    std::uint64_t nextSequence_ = 0;
};

}  // namespace opticalnet

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

// The numeric value is the priority among events with the SAME timestamp (lower is processed first).
// Releases go before arrivals so that capacity freed at time t is usable by a request arriving at t.
enum class EventType : std::uint8_t {
    ConnectionRelease = 0,
    ConnectionArrival = 1,
};

struct SimulationEvent {
    SimTime time;
    EventType type;
    ConnectionId connection;   // the request (arrival) or the established connection (release)
    std::uint64_t sequence;    // creation order, assigned by the EventQueue
};

// Time-ordered queue of events. Total, deterministic processing order:
//   1. earlier timestamp first
//   2. at equal timestamps, by EventType (releases before arrivals)
//   3. at equal timestamp and type, by sequence number (the order in which events were scheduled)
class EventQueue {
public:
    // Schedules an event and returns its sequence number.
    //   InvalidArgument: `time` is negative, NaN or infinite
    [[nodiscard]] Result<std::uint64_t> schedule(SimTime time, EventType type, ConnectionId connection);

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

#include <cmath>
#include <stdexcept>

#include "opticalnet/simulation/SimulationEvent.hpp"

namespace opticalnet {

Result<std::uint64_t> EventQueue::schedule(SimTime time, EventType type, ConnectionId connection) {
    if (!std::isfinite(time) || time < 0.0)
        return Error{ErrorCode::InvalidArgument, "event time must be a non-negative finite number"};
    const std::uint64_t sequence = nextSequence_++;
    queue_.push(SimulationEvent{time, type, connection, sequence});
    return sequence;
}

const SimulationEvent& EventQueue::peek() const {
    if (queue_.empty()) throw std::logic_error("EventQueue::peek on an empty queue");
    return queue_.top();
}

SimulationEvent EventQueue::pop() {
    if (queue_.empty()) throw std::logic_error("EventQueue::pop on an empty queue");
    SimulationEvent next = queue_.top();
    queue_.pop();
    return next;
}

}  // namespace opticalnet

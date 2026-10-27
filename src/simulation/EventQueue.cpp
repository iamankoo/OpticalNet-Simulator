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

Result<std::uint64_t> EventQueue::scheduleResource(SimTime time, EventType type, std::uint32_t resource) {
    if (!std::isfinite(time) || time < 0.0)
        return Error{ErrorCode::InvalidArgument, "event time must be a non-negative finite number"};
    const std::uint64_t sequence = nextSequence_++;
    queue_.push(SimulationEvent{time, type, ConnectionId{0}, sequence, resource});
    return sequence;
}

bool isFailureEvent(EventType type) noexcept {
    return type == EventType::LinkFailure || type == EventType::LinkRecovery || type == EventType::NodeFailure ||
           type == EventType::NodeRecovery;
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

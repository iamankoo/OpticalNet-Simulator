#include "opticalnet/simulation/SimulationEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "opticalnet/resources/ConnectionManager.hpp"

namespace opticalnet {

namespace {

struct Pending {  // an arrival waiting in the queue
    ConnectionRequest request;
    SimTime lifetime;
};

// One run. Owns every piece of dynamic state (connection manager, queue, bookkeeping); the topology is only read.
class Runner {
public:
    Runner(const Topology& topology, const SimulationConfig& config)
        : config_(config), manager_(topology) {}

    void setGenerator(RequestGenerator generator) { generator_.emplace(std::move(generator)); }

    // Put an arrival on the timeline. The time has already been validated by the caller.
    void scheduleArrival(SimTime time, ConnectionRequest request, SimTime lifetime) {
        const ConnectionId id = request.id;
        const std::uint64_t sequence = queue_.schedule(time, EventType::ConnectionArrival, id).value();
        pending_.emplace(sequence, Pending{std::move(request), lifetime});
    }

    Result<SimulationResult> execute() {
        metrics_.totalCapacityChannels = manager_.resources().totalCapacity();
        pullNextGenerated();

        // Events strictly in (time, type, sequence) order; the clock jumps from event to event.
        while (!queue_.empty() && queue_.peek().time <= config_.endTime) {
            const SimulationEvent event = queue_.pop();
            advanceClockTo(event.time);
            if (event.type == EventType::ConnectionArrival) {
                handleArrival(event);
                pullNextGenerated();
            } else if (auto released = handleRelease(event); !released.ok()) {
                return released.error();
            }
            ++metrics_.eventsProcessed;
            lastEventTime_ = event.time;
            if (allocatedChannels_ > metrics_.peakAllocatedChannels) metrics_.peakAllocatedChannels = allocatedChannels_;
            if (active_ > metrics_.peakActiveConnections) metrics_.peakActiveConnections = active_;
        }

        // A run stopped by its request cap, with every release done, ends at its last event; otherwise at the horizon.
        const bool stoppedByCap = queue_.empty() && generator_ && generator_->limitReached();
        const SimTime finalTime = stoppedByCap ? lastEventTime_ : config_.endTime;
        advanceClockTo(finalTime);
        return finish(finalTime);
    }

private:
    void pullNextGenerated() {
        if (!generator_) return;
        if (auto next = generator_->next()) scheduleArrival(next->arrival, std::move(next->request), next->lifetime);
    }

    // Allocation is constant between events, so the integral advances in exact rectangles.
    void advanceClockTo(SimTime time) {
        allocatedTimeIntegral_ += static_cast<double>(allocatedChannels_) * (time - clock_);
        clock_ = time;
    }

    void handleArrival(const SimulationEvent& event) {
        const auto node = pending_.extract(event.sequence);
        const ConnectionRequest& request = node.mapped().request;
        ++metrics_.totalRequests;

        auto established = manager_.establish(request);
        if (established.ok()) {
            const Connection& connection = established.value();
            const std::size_t hops = connection.path().hopCount();
            ++metrics_.acceptedRequests;
            allocatedChannels_ += static_cast<std::uint64_t>(connection.capacityChannels()) * hops;
            ++active_;
            sumHops_ += hops;
            sumCost_ += connection.path().cost();
            if (hops > metrics_.maxHopCount) metrics_.maxHopCount = hops;
            // The release is scheduled even if it lies beyond the horizon: it simply stays in the queue.
            // The sum is clamped so absurdly long lifetimes cannot overflow to infinity.
            const SimTime releaseTime = std::min(event.time + node.mapped().lifetime, std::numeric_limits<SimTime>::max());
            (void)queue_.schedule(releaseTime, EventType::ConnectionRelease, request.id).value();
            trace(event, request, true, std::nullopt, hops);
        } else {
            ++metrics_.rejectedRequests;
            ++metrics_.rejectionsByReason[established.error().code];
            trace(event, request, false, established.error().code, 0);
        }
    }

    Result<void> handleRelease(const SimulationEvent& event) {
        const Connection* connection = manager_.find(event.connection);
        if (connection == nullptr)
            return Error{ErrorCode::InconsistentState, "release event for connection " + std::to_string(event.connection.value()) +
                                                           " which is not active"};
        const Connection snapshot = *connection;  // the registry entry disappears on release
        if (auto released = manager_.release(event.connection); !released.ok()) return released;
        allocatedChannels_ -= static_cast<std::uint64_t>(snapshot.capacityChannels()) * snapshot.path().hopCount();
        --active_;
        ++metrics_.totalReleases;
        trace(event, snapshot.request(), true, std::nullopt, snapshot.path().hopCount());
        return {};
    }

    void trace(const SimulationEvent& event, const ConnectionRequest& request, bool success,
               std::optional<ErrorCode> reason, std::size_t hops) {
        if (!config_.recordTrace) return;
        trace_.push_back(TraceEntry{event.time, event.type, request.id, request.source, request.destination,
                                    request.capacityChannels, success, reason, hops});
    }

    Result<SimulationResult> finish(SimTime finalTime) {
        // Sanity checks: the engine's own bookkeeping must agree with the resource layer, which must be consistent.
        const ResourceManager& resources = manager_.resources();
        if (!manager_.validate().valid() || resources.totalAllocated() != allocatedChannels_ || manager_.activeCount() != active_)
            return Error{ErrorCode::InconsistentState, "resource state disagrees with the simulation's bookkeeping at the end of the run"};

        SimulationMetrics& m = metrics_;
        m.duration = finalTime;
        if (m.totalRequests > 0) {
            m.acceptanceRate = static_cast<double>(m.acceptedRequests) / static_cast<double>(m.totalRequests);
            m.blockingRate = static_cast<double>(m.rejectedRequests) / static_cast<double>(m.totalRequests);
        }
        if (m.totalCapacityChannels > 0 && finalTime > 0.0)
            m.averageUtilization = allocatedTimeIntegral_ / (static_cast<double>(m.totalCapacityChannels) * finalTime);
        m.finalAllocatedChannels = resources.totalAllocated();
        m.finalAvailableChannels = resources.totalAvailable();
        if (m.acceptedRequests > 0) {
            m.averagePathCost = sumCost_ / static_cast<double>(m.acceptedRequests);
            m.averageHopCount = static_cast<double>(sumHops_) / static_cast<double>(m.acceptedRequests);
        }
        m.activeConnectionsAtEnd = active_;

        SimulationResult result;
        result.seed = config_.seed;
        result.metrics = std::move(metrics_);
        result.trace = std::move(trace_);
        return result;
    }

    SimulationConfig config_;
    ConnectionManager manager_;
    EventQueue queue_;
    std::optional<RequestGenerator> generator_;
    std::unordered_map<std::uint64_t, Pending> pending_;  // keyed by the arrival event's sequence number

    SimulationMetrics metrics_;
    std::vector<TraceEntry> trace_;
    SimTime clock_ = 0.0;
    SimTime lastEventTime_ = 0.0;
    std::uint64_t allocatedChannels_ = 0;  // channel-links currently held, kept incrementally
    std::uint64_t active_ = 0;
    double allocatedTimeIntegral_ = 0.0;
    std::size_t sumHops_ = 0;
    double sumCost_ = 0.0;
};

}  // namespace

Result<SimulationResult> SimulationEngine::run(const Topology& topology, const SimulationConfig& config) const {
    auto generator = RequestGenerator::create(topology, config);  // validates the config
    if (!generator.ok()) return generator.error();
    Runner runner(topology, config);
    runner.setGenerator(std::move(generator).value());
    return runner.execute();
}

Result<SimulationResult> SimulationEngine::runScript(const Topology& topology, std::span<const ScriptedArrival> arrivals,
                                                     const SimulationConfig& config) const {
    if (!std::isfinite(config.endTime) || config.endTime < 0.0)
        return Error{ErrorCode::InvalidArgument, "simulation endTime must be a non-negative finite number"};
    for (const ScriptedArrival& a : arrivals) {
        if (!std::isfinite(a.time) || a.time < 0.0)
            return Error{ErrorCode::InvalidArgument, "scripted arrival time must be a non-negative finite number"};
        if (!(std::isfinite(a.lifetime) && a.lifetime > 0.0))
            return Error{ErrorCode::InvalidArgument, "scripted lifetime must be a positive finite number"};
    }
    SimulationConfig scripted = config;
    scripted.seed = 0;
    Runner runner(topology, scripted);
    for (const ScriptedArrival& a : arrivals) runner.scheduleArrival(a.time, a.request, a.lifetime);
    return runner.execute();
}

}  // namespace opticalnet

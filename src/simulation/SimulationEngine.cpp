#include "opticalnet/simulation/SimulationEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "opticalnet/failures/FailureState.hpp"
#include "opticalnet/resources/ConnectionManager.hpp"
#include "opticalnet/simulation/RandomFailures.hpp"

namespace opticalnet {

namespace {

struct Pending {  // an arrival waiting in the queue
    ConnectionRequest request;
    SimTime lifetime;
};

struct LostInfo {  // a connection lost to a failure whose scheduled end has not been reached yet
    SimTime lostAt;
    SimTime scheduledEnd;
};

EventType eventTypeOf(FailureAction action) {
    switch (action) {
        case FailureAction::FailLink: return EventType::LinkFailure;
        case FailureAction::RecoverLink: return EventType::LinkRecovery;
        case FailureAction::FailNode: return EventType::NodeFailure;
        case FailureAction::RecoverNode: return EventType::NodeRecovery;
    }
    return EventType::LinkFailure;
}

// One run. Owns every piece of dynamic state (failure overlay, connection manager, queue, bookkeeping); the
// topology is only read, and nothing here is shared with any other run.
class Runner {
public:
    Runner(const Topology& topology, const SimulationConfig& config)
        : config_(config), failures_(topology), manager_(topology) {
        // The failure overlay is how routing learns what is unavailable (same linkFilter mechanism as capacity).
        manager_.setAvailability([this](const FiberLink& link) { return failures_.isLinkUsable(link); });
    }
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    void setGenerator(RequestGenerator generator) { generator_.emplace(std::move(generator)); }

    // Put an arrival on the timeline. The time has already been validated by the caller.
    void scheduleArrival(SimTime time, ConnectionRequest request, SimTime lifetime) {
        const ConnectionId id = request.id;
        const std::uint64_t sequence = queue_.schedule(time, EventType::ConnectionArrival, id).value();
        pending_.emplace(sequence, Pending{std::move(request), lifetime});
    }

    // Put validated failure/recovery actions on the timeline.
    void scheduleFailures(const std::vector<ScriptedFailure>& schedule) {
        for (const ScriptedFailure& f : schedule) (void)queue_.scheduleResource(f.time, eventTypeOf(f.action), f.id).value();
    }

    Result<SimulationResult> execute() {
        metrics_.totalCapacityChannels = manager_.resources().totalCapacity();
        pullNextGenerated();

        // Events strictly in (time, type, sequence) order; the clock jumps from event to event.
        while (!queue_.empty() && queue_.peek().time <= config_.endTime) {
            const SimulationEvent event = queue_.pop();
            advanceClockTo(event.time);
            Result<void> handled;
            switch (event.type) {
                case EventType::ConnectionArrival:
                    handleArrival(event);
                    pullNextGenerated();
                    break;
                case EventType::ConnectionRelease: handled = handleRelease(event); break;
                case EventType::LinkFailure:
                case EventType::NodeFailure: handled = handleFailure(event); break;
                case EventType::LinkRecovery:
                case EventType::NodeRecovery: handled = handleRecovery(event); break;
            }
            if (!handled.ok()) return handled.error();
            ++metrics_.eventsProcessed;
            lastEventTime_ = event.time;
            if (allocatedChannels_ > metrics_.peakAllocatedChannels) metrics_.peakAllocatedChannels = allocatedChannels_;
            if (active_ > metrics_.peakActiveConnections) metrics_.peakActiveConnections = active_;
        }

        // A run stopped by its request cap, with every event done, ends at its last event; otherwise at the horizon.
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

    static std::uint64_t channelLinks(const Connection& c) {
        return static_cast<std::uint64_t>(c.capacityChannels()) * c.path().hopCount();
    }

    // ------------------------------------------------------------------ connection events

    void handleArrival(const SimulationEvent& event) {
        const auto node = pending_.extract(event.sequence);
        const ConnectionRequest& request = node.mapped().request;
        ++metrics_.totalRequests;

        // An id whose earlier connection was lost to a failure stays "in use" until that connection's scheduled end.
        auto established = lost_.contains(request.id)
                               ? Result<Connection>{Error{ErrorCode::DuplicateId, "connection id is still held by a lost connection"}}
                               : manager_.establish(request);
        if (established.ok()) {
            const Connection& connection = established.value();
            const std::size_t hops = connection.path().hopCount();
            ++metrics_.acceptedRequests;
            allocatedChannels_ += channelLinks(connection);
            ++active_;
            sumHops_ += hops;
            sumCost_ += connection.path().cost();
            if (hops > metrics_.maxHopCount) metrics_.maxHopCount = hops;
            // The release is scheduled even if it lies beyond the horizon: it simply stays in the queue.
            // The sum is clamped so absurdly long lifetimes cannot overflow to infinity.
            const SimTime releaseTime = std::min(event.time + node.mapped().lifetime, std::numeric_limits<SimTime>::max());
            (void)queue_.schedule(releaseTime, EventType::ConnectionRelease, request.id).value();
            scheduledEnd_[request.id] = releaseTime;
            trace(event, request, true, std::nullopt, hops);
        } else {
            ++metrics_.rejectedRequests;
            ++metrics_.rejectionsByReason[established.error().code];
            trace(event, request, false, established.error().code, 0);
        }
    }

    Result<void> handleRelease(const SimulationEvent& event) {
        const Connection* connection = manager_.find(event.connection);
        if (connection == nullptr) {
            // Not active: either it was lost to a failure (its scheduled end simply arrives; nothing to release), or a bug.
            const auto lost = lost_.find(event.connection);
            if (lost == lost_.end())
                return Error{ErrorCode::InconsistentState, "release event for connection " + std::to_string(event.connection.value()) +
                                                               " which is not active"};
            metrics_.totalConnectionDowntime += event.time - lost->second.lostAt;
            lost_.erase(lost);
            scheduledEnd_.erase(event.connection);
            return {};
        }
        const Connection snapshot = *connection;  // the registry entry disappears on release
        if (auto released = manager_.release(event.connection); !released.ok()) return released;
        allocatedChannels_ -= channelLinks(snapshot);
        --active_;
        ++metrics_.totalReleases;
        scheduledEnd_.erase(event.connection);
        trace(event, snapshot.request(), true, std::nullopt, snapshot.path().hopCount());
        return {};
    }

    // ------------------------------------------------------------------- failure events

    Result<void> handleFailure(const SimulationEvent& event) {
        const bool isLink = event.type == EventType::LinkFailure;
        Result<void> applied = isLink ? failures_.failLink(LinkId{event.resource}) : failures_.failNode(NodeId{event.resource});
        if (!applied.ok()) return applied;
        ++metrics_.totalFailureEvents;
        ++(isLink ? metrics_.linkFailures : metrics_.nodeFailures);
        traceResource(event);

        // Detection: every active connection whose current path uses a failed link or node (ascending id).
        std::vector<ConnectionId> affected;
        for (const auto& [id, connection] : manager_.activeConnections()) {
            if (failures_.isPathAffected(connection.path())) affected.push_back(id);
        }
        if (affected.empty()) return {};

        // Phase 1: take every affected connection off the network, so the capacity they hold is visible to all the
        // reroutes (otherwise the first reroute could be refused because a later victim still occupies a shared link).
        std::vector<Connection> detached;
        detached.reserve(affected.size());
        for (const ConnectionId id : affected) {
            ++metrics_.connectionsAffected;
            auto removed = manager_.detach(id);
            if (!removed.ok()) return removed.error();
            allocatedChannels_ -= channelLinks(removed.value());
            --active_;
            traceConsequence(event, removed.value(), TraceKind::Affected, true, std::nullopt, removed.value().path().hopCount());
            detached.push_back(std::move(removed).value());
        }

        // Phase 2: policy. A reroute re-establishes the connection under its own id with its own demand and its
        // original constraints (establish routes over the surviving network and allocates atomically).
        for (const Connection& old : detached) {
            if (config_.recoveryPolicy == RecoveryPolicy::Reroute) {
                auto rerouted = manager_.establish(old.request());
                if (rerouted.ok()) {
                    ++metrics_.connectionsRerouted;
                    allocatedChannels_ += channelLinks(rerouted.value());
                    ++active_;
                    traceConsequence(event, rerouted.value(), TraceKind::Rerouted, true, std::nullopt, rerouted.value().path().hopCount());
                    continue;
                }
                lose(event, old, rerouted.error().code);
            } else {
                lose(event, old, std::nullopt);
            }
        }
        return {};
    }

    void lose(const SimulationEvent& event, const Connection& connection, std::optional<ErrorCode> reason) {
        ++metrics_.connectionsLost;
        lost_[connection.id()] = LostInfo{event.time, scheduledEnd_.at(connection.id())};
        traceConsequence(event, connection, TraceKind::Lost, false, reason, 0);
    }

    Result<void> handleRecovery(const SimulationEvent& event) {
        const bool isLink = event.type == EventType::LinkRecovery;
        Result<void> applied = isLink ? failures_.recoverLink(LinkId{event.resource}) : failures_.recoverNode(NodeId{event.resource});
        if (!applied.ok()) return applied;
        ++(isLink ? metrics_.linkRecoveries : metrics_.nodeRecoveries);
        traceResource(event);
        return {};
    }

    // --------------------------------------------------------------------------- trace

    void trace(const SimulationEvent& event, const ConnectionRequest& request, bool success,
               std::optional<ErrorCode> reason, std::size_t hops) {
        if (!config_.recordTrace) return;
        trace_.push_back(TraceEntry{event.time, event.type, request.id, request.source, request.destination,
                                    request.capacityChannels, success, reason, hops});
    }

    void traceResource(const SimulationEvent& event) {
        if (!config_.recordTrace) return;
        trace_.push_back(TraceEntry{event.time, event.type, ConnectionId{0}, NodeId{0}, NodeId{0}, 0, true, std::nullopt, 0,
                                    event.resource, TraceKind::Event});
    }

    void traceConsequence(const SimulationEvent& event, const Connection& connection, TraceKind kind, bool success,
                          std::optional<ErrorCode> reason, std::size_t hops) {
        if (!config_.recordTrace) return;
        const ConnectionRequest& r = connection.request();
        trace_.push_back(TraceEntry{event.time, event.type, r.id, r.source, r.destination, r.capacityChannels, success, reason, hops,
                                    event.resource, kind});
    }

    // ---------------------------------------------------------------------------- end

    Result<SimulationResult> finish(SimTime finalTime) {
        // Sanity checks: the engine's own bookkeeping must agree with the resource layer, which must be consistent,
        // and no active connection may sit on a failed resource.
        const ResourceManager& resources = manager_.resources();
        if (!manager_.validate().valid() || resources.totalAllocated() != allocatedChannels_ || manager_.activeCount() != active_)
            return Error{ErrorCode::InconsistentState, "resource state disagrees with the simulation's bookkeeping at the end of the run"};
        for (const auto& [id, connection] : manager_.activeConnections()) {
            if (failures_.isPathAffected(connection.path()))
                return Error{ErrorCode::InconsistentState, "connection " + std::to_string(id.value()) + " is active on a failed resource"};
        }

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
        if (m.connectionsAffected > 0)
            m.recoverySuccessRate = static_cast<double>(m.connectionsRerouted) / static_cast<double>(m.connectionsAffected);
        for (const auto& [id, info] : lost_) m.totalConnectionDowntime += std::min(info.scheduledEnd, finalTime) - info.lostAt;
        m.failedLinksAtEnd = failures_.failedLinks().size();
        m.failedNodesAtEnd = failures_.failedNodes().size();

        SimulationResult result;
        result.seed = config_.seed;
        result.metrics = std::move(metrics_);
        result.trace = std::move(trace_);
        return result;
    }

    SimulationConfig config_;
    FailureState failures_;       // declared before manager_: the manager's availability predicate refers to it
    ConnectionManager manager_;
    EventQueue queue_;
    std::optional<RequestGenerator> generator_;
    std::unordered_map<std::uint64_t, Pending> pending_;  // keyed by the arrival event's sequence number
    std::unordered_map<ConnectionId, SimTime> scheduledEnd_;  // when each live (or lost) connection was due to end
    std::unordered_map<ConnectionId, LostInfo> lost_;

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

    // The failure schedule is fixed up front (and independent of traffic): scripted, or generated from the seed.
    std::vector<ScriptedFailure> schedule = config.failureSchedule;
    if (config.randomFailures) schedule = generateRandomFailures(topology, *config.randomFailures, config.endTime, config.seed);
    if (auto valid = validateFailureSchedule(topology, schedule); !valid.ok()) return valid.error();

    Runner runner(topology, config);
    runner.scheduleFailures(schedule);
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
    if (auto valid = validateFailureSchedule(topology, config.failureSchedule); !valid.ok()) return valid.error();

    SimulationConfig scripted = config;
    scripted.seed = 0;
    Runner runner(topology, scripted);
    runner.scheduleFailures(scripted.failureSchedule);
    for (const ScriptedArrival& a : arrivals) runner.scheduleArrival(a.time, a.request, a.lifetime);
    return runner.execute();
}

}  // namespace opticalnet

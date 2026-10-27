// pybind11 bindings for OpticalNet (Phase 9).
//
// This file is a thin translation layer: every algorithm (graph, routing, resource accounting, simulation,
// failures, studies, validation) is the existing C++ implementation. Python sees plain values (ints, floats,
// strings, dicts, lists) and a few small classes; it never sees Result<T>, StrongId<Tag> or raw pointers.
//
// Ownership: a Topology is immutable once loaded and held by std::shared_ptr<const Topology>. Every object that
// references it (ConnectionSession) co-owns that pointer, so the topology cannot disappear while a manager
// depends on it, and nothing is ever handed to Python by reference to C++ internals (results are copied into
// Python values). Errors: expected failures (Result errors) become a hierarchy of Python exceptions, one per
// ErrorCode, each with a stable `code` string; unexpected C++ exceptions keep pybind11's default translation.
//
// GIL: long-running operations (simulate, run_study, route, validation) copy their inputs, release the GIL and
// run on C++ data only, so several Python threads (e.g. FastAPI workers) can run simulations concurrently.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "opticalnet/failures/FailureState.hpp"
#include "opticalnet/resources/ConnectionManager.hpp"
#include "opticalnet/routing/RoutingEngine.hpp"
#include "opticalnet/simulation/SimulationEngine.hpp"
#include "opticalnet/simulation/SimulationStudy.hpp"
#include "opticalnet/topology/TopologyIo.hpp"
#include "opticalnet/validation/Invariants.hpp"

namespace py = pybind11;
using namespace opticalnet;

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------------------------------------------

class OpticalNetException : public std::exception {
public:
    explicit OpticalNetException(Error error) : error_(std::move(error)), text_(error_.message) {}
    [[nodiscard]] const char* what() const noexcept override { return text_.c_str(); }
    [[nodiscard]] ErrorCode code() const noexcept { return error_.code; }

private:
    Error error_;
    std::string text_;
};

const char* codeName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument: return "INVALID_ARGUMENT";
        case ErrorCode::DuplicateId: return "DUPLICATE_ID";
        case ErrorCode::NotFound: return "NOT_FOUND";
        case ErrorCode::ConstraintViolation: return "CONSTRAINT_VIOLATION";
        case ErrorCode::ParseError: return "PARSE_ERROR";
        case ErrorCode::NoRoute: return "NO_ROUTE";
        case ErrorCode::NoFeasibleRoute: return "NO_FEASIBLE_ROUTE";
        case ErrorCode::InsufficientCapacity: return "INSUFFICIENT_CAPACITY";
        case ErrorCode::InconsistentState: return "INCONSISTENT_STATE";
        case ErrorCode::InternalError: return "INTERNAL_ERROR";
        case ErrorCode::InvalidTransition: return "INVALID_TRANSITION";
        case ErrorCode::ResourceUnavailable: return "RESOURCE_UNAVAILABLE";
    }
    return "INTERNAL_ERROR";
}

// Python exception classes, one per ErrorCode. Intentionally leaked: they must outlive interpreter shutdown ordering.
std::map<ErrorCode, py::object>* g_exceptions = nullptr;

[[noreturn]] void raise(Error error) { throw OpticalNetException(std::move(error)); }
[[noreturn]] void raise(ErrorCode code, std::string message) { raise(Error{code, std::move(message)}); }

template <class T>
T unwrap(Result<T>&& result) {
    if (!result.ok()) raise(result.error());
    return std::move(result).value();
}
void check(const Result<void>& result) {
    if (!result.ok()) raise(result.error());
}

std::uint32_t toId(std::int64_t value, const char* what) {
    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        raise(ErrorCode::InvalidArgument, std::string(what) + " " + std::to_string(value) + " is outside the valid id range");
    }
    return static_cast<std::uint32_t>(value);
}
NodeId nodeId(std::int64_t v) { return NodeId(toId(v, "node id")); }
LinkId linkId(std::int64_t v) { return LinkId(toId(v, "link id")); }
ConnectionId connId(std::int64_t v) { return ConnectionId(toId(v, "connection id")); }

// ---------------------------------------------------------------------------------------------------------------
// Enum <-> string
// ---------------------------------------------------------------------------------------------------------------

CostMetric parseMetric(const std::string& s) {
    if (s == "hop_count") return CostMetric::HopCount;
    if (s == "distance") return CostMetric::Distance;
    if (s == "administrative") return CostMetric::Administrative;
    raise(ErrorCode::InvalidArgument, "unknown metric '" + s + "' (expected hop_count, distance or administrative)");
}
const char* metricName(CostMetric m) {
    switch (m) {
        case CostMetric::HopCount: return "hop_count";
        case CostMetric::Distance: return "distance";
        case CostMetric::Administrative: return "administrative";
    }
    return "administrative";
}
Timing parseTiming(const std::string& s) {
    if (s == "exponential") return Timing::Exponential;
    if (s == "fixed") return Timing::Fixed;
    raise(ErrorCode::InvalidArgument, "unknown timing '" + s + "' (expected exponential or fixed)");
}
const char* timingName(Timing t) { return t == Timing::Fixed ? "fixed" : "exponential"; }
RecoveryPolicy parsePolicy(const std::string& s) {
    if (s == "reroute") return RecoveryPolicy::Reroute;
    if (s == "no_recovery") return RecoveryPolicy::NoRecovery;
    raise(ErrorCode::InvalidArgument, "unknown recovery policy '" + s + "' (expected reroute or no_recovery)");
}
const char* policyName(RecoveryPolicy p) { return p == RecoveryPolicy::Reroute ? "reroute" : "no_recovery"; }
FailureAction parseAction(const std::string& s) {
    if (s == "fail_link") return FailureAction::FailLink;
    if (s == "recover_link") return FailureAction::RecoverLink;
    if (s == "fail_node") return FailureAction::FailNode;
    if (s == "recover_node") return FailureAction::RecoverNode;
    raise(ErrorCode::InvalidArgument,
          "unknown failure action '" + s + "' (expected fail_link, recover_link, fail_node or recover_node)");
}
const char* actionName(FailureAction a) {
    switch (a) {
        case FailureAction::FailLink: return "fail_link";
        case FailureAction::RecoverLink: return "recover_link";
        case FailureAction::FailNode: return "fail_node";
        case FailureAction::RecoverNode: return "recover_node";
    }
    return "fail_link";
}
const char* eventName(EventType t) {
    switch (t) {
        case EventType::LinkRecovery: return "link_recovery";
        case EventType::NodeRecovery: return "node_recovery";
        case EventType::LinkFailure: return "link_failure";
        case EventType::NodeFailure: return "node_failure";
        case EventType::ConnectionRelease: return "connection_release";
        case EventType::ConnectionArrival: return "connection_arrival";
    }
    return "connection_arrival";
}
const char* traceKindName(TraceKind k) {
    switch (k) {
        case TraceKind::Event: return "event";
        case TraceKind::Affected: return "affected";
        case TraceKind::Rerouted: return "rerouted";
        case TraceKind::Lost: return "lost";
    }
    return "event";
}

// ---------------------------------------------------------------------------------------------------------------
// Value -> Python conversion (plain dicts and lists; JSON-friendly)
// ---------------------------------------------------------------------------------------------------------------

template <class Id>
std::vector<std::uint32_t> values(const std::vector<Id>& ids) {
    std::vector<std::uint32_t> out;
    out.reserve(ids.size());
    for (const auto& id : ids) out.push_back(id.value());
    return out;
}

py::dict pathDict(const Path& path) {
    py::dict d;
    d["nodes"] = values(path.nodes());
    d["links"] = values(path.links());
    d["cost"] = path.cost();
    d["hop_count"] = path.hopCount();
    d["metric"] = metricName(path.metric());
    return d;
}

py::dict connectionDict(const Connection& c) {
    py::dict d;
    d["id"] = c.id().value();
    d["source"] = c.request().source.value();
    d["destination"] = c.request().destination.value();
    d["demand"] = c.capacityChannels();
    d["path"] = pathDict(c.path());
    return d;
}

py::dict nodeDict(const Node& n) {
    py::dict d;
    d["id"] = n.id().value();
    d["name"] = n.name();
    if (n.switchingElement()) d["switching_element"] = n.switchingElement()->value();
    else d["switching_element"] = py::none();
    d["transceivers"] = values(n.transceivers());
    return d;
}

py::dict linkDict(const FiberLink& l) {
    py::dict d;
    d["id"] = l.id().value();
    d["name"] = l.name();
    d["source"] = l.source().value();
    d["target"] = l.target().value();
    d["length_km"] = l.lengthKm();
    d["attenuation_db_per_km"] = l.attenuationDbPerKm();
    d["capacity_channels"] = l.capacityChannels();
    d["direction"] = std::string(toString(l.direction()));
    d["cost"] = l.administrativeCost();
    return d;
}

py::dict usageDict(LinkId link, const LinkUsage& u) {
    py::dict d;
    d["link"] = link.value();
    d["total"] = u.total;
    d["allocated"] = u.allocated;
    d["available"] = u.available;
    return d;
}

py::list issuesList(const ValidationReport& report) {
    py::list out;
    for (const auto& issue : report.issues) {
        py::dict d;
        d["severity"] = issue.severity == Severity::Error ? "error" : "warning";
        d["code"] = std::string(toString(issue.code));
        d["message"] = issue.message;
        out.append(d);
    }
    return out;
}

py::list failuresList(const ValidationResult& result) {
    py::list out;
    for (const auto& f : result.failures()) {
        py::dict d;
        d["check"] = f.check;
        d["message"] = f.message;
        out.append(d);
    }
    return out;
}

py::dict statsDict(const SampleStatistics& s) {
    py::dict d;
    d["count"] = s.count;
    d["mean"] = s.mean;
    d["std_dev"] = s.stdDev;
    d["standard_error"] = s.standardError;
    d["confidence_half_width_95"] = s.confidenceHalfWidth95;
    // Presentation only: the interval is mean +/- the half-width computed by C++.
    d["confidence_interval_95"] = py::make_tuple(s.mean - s.confidenceHalfWidth95, s.mean + s.confidenceHalfWidth95);
    return d;
}

py::dict reasonsDict(const std::map<ErrorCode, std::uint64_t>& reasons) {
    py::dict d;
    for (const auto& [code, count] : reasons) d[codeName(code)] = count;
    return d;
}

py::dict metricsDict(const SimulationMetrics& m) {
    py::dict d;
    d["total_requests"] = m.totalRequests;
    d["accepted_requests"] = m.acceptedRequests;
    d["rejected_requests"] = m.rejectedRequests;
    d["rejections_by_reason"] = reasonsDict(m.rejectionsByReason);
    d["acceptance_rate"] = m.acceptanceRate;
    d["blocking_rate"] = m.blockingRate;
    d["total_capacity_channels"] = m.totalCapacityChannels;
    d["peak_allocated_channels"] = m.peakAllocatedChannels;
    d["average_utilization"] = m.averageUtilization;
    d["final_allocated_channels"] = m.finalAllocatedChannels;
    d["final_available_channels"] = m.finalAvailableChannels;
    d["average_path_cost"] = m.averagePathCost;
    d["average_hop_count"] = m.averageHopCount;
    d["max_hop_count"] = m.maxHopCount;
    d["active_connections_at_end"] = m.activeConnectionsAtEnd;
    d["peak_active_connections"] = m.peakActiveConnections;
    d["total_releases"] = m.totalReleases;
    d["total_failure_events"] = m.totalFailureEvents;
    d["link_failures"] = m.linkFailures;
    d["node_failures"] = m.nodeFailures;
    d["link_recoveries"] = m.linkRecoveries;
    d["node_recoveries"] = m.nodeRecoveries;
    d["connections_affected"] = m.connectionsAffected;
    d["connections_rerouted"] = m.connectionsRerouted;
    d["connections_lost"] = m.connectionsLost;
    d["recovery_success_rate"] = m.recoverySuccessRate;
    d["total_connection_downtime"] = m.totalConnectionDowntime;
    d["failed_links_at_end"] = m.failedLinksAtEnd;
    d["failed_nodes_at_end"] = m.failedNodesAtEnd;
    d["events_processed"] = m.eventsProcessed;
    d["duration"] = m.duration;
    return d;
}

py::list traceList(const std::vector<TraceEntry>& trace) {
    py::list out;
    for (const auto& e : trace) {
        py::dict d;
        d["time"] = e.time;
        d["event"] = eventName(e.type);
        d["kind"] = traceKindName(e.kind);
        d["connection"] = e.connection.value();
        d["source"] = e.source.value();
        d["destination"] = e.destination.value();
        d["demand"] = e.capacityChannels;
        d["success"] = e.success;
        if (e.reason) d["reason"] = codeName(*e.reason);
        else d["reason"] = py::none();
        d["hops"] = e.hops;
        d["resource"] = e.resource;
        out.append(d);
    }
    return out;
}

py::dict resultDict(const SimulationResult& r) {
    py::dict d;
    d["seed"] = r.seed;
    d["metrics"] = metricsDict(r.metrics);
    d["trace"] = traceList(r.trace);
    return d;
}

// ---------------------------------------------------------------------------------------------------------------
// Topology
// ---------------------------------------------------------------------------------------------------------------

struct PyTopology {
    std::shared_ptr<const Topology> topology;
};

PyTopology wrap(Topology topology) { return PyTopology{std::make_shared<const Topology>(std::move(topology))}; }

PyTopology topologyFromJson(const std::string& text) {
    auto result = [&] {
        py::gil_scoped_release release;
        return loadTopologyFromJson(text);
    }();
    return wrap(unwrap(std::move(result)));
}

PyTopology topologyFromFile(const std::string& path) {
    auto result = [&] {
        py::gil_scoped_release release;
        return loadTopologyFromFile(path);
    }();
    return wrap(unwrap(std::move(result)));
}

py::dict topologyValidate(const Topology& topology) {
    ValidationReport report;
    ValidationResult invariants;
    {
        py::gil_scoped_release release;
        report = topology.validate();
        invariants = checkTopology(topology);
    }
    py::dict d;
    d["valid"] = report.valid() && invariants.passed();
    d["errors"] = report.errorCount();
    d["warnings"] = report.warningCount();
    d["issues"] = issuesList(report);
    d["invariant_failures"] = failuresList(invariants);
    return d;
}

// ---------------------------------------------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------------------------------------------

std::unordered_set<NodeId> nodeSet(const std::vector<std::int64_t>& v) {
    std::unordered_set<NodeId> s;
    for (auto x : v) s.insert(nodeId(x));
    return s;
}
std::unordered_set<LinkId> linkSet(const std::vector<std::int64_t>& v) {
    std::unordered_set<LinkId> s;
    for (auto x : v) s.insert(linkId(x));
    return s;
}

RoutingConstraints makeConstraints(const std::string& metric, std::optional<double> maxCost, std::optional<std::int64_t> maxHops,
                                   std::int64_t minLinkCapacity, const std::vector<std::int64_t>& blockedNodes,
                                   const std::vector<std::int64_t>& blockedLinks) {
    RoutingConstraints c;
    c.metric = parseMetric(metric);
    c.maxCost = maxCost;
    if (maxHops) c.maxHops = toId(*maxHops, "max_hops");
    c.minLinkCapacityChannels = toId(minLinkCapacity, "min_link_capacity");
    c.blockedNodes = nodeSet(blockedNodes);
    c.blockedLinks = linkSet(blockedLinks);
    return c;
}

std::vector<std::int64_t> sortedValues(const auto& set) {
    std::vector<std::int64_t> out;
    for (const auto& id : set) out.push_back(id.value());
    std::sort(out.begin(), out.end());
    return out;
}

py::dict routeImpl(const PyTopology& t, std::int64_t source, std::int64_t destination, const RoutingConstraints& constraints) {
    const auto topology = t.topology;
    const NodeId s = nodeId(source);
    const NodeId d = nodeId(destination);
    const RoutingConstraints copy = constraints;  // the GIL is released below: do not read Python-owned state then
    auto result = [&] {
        py::gil_scoped_release release;
        return RoutingEngine{}.findPath(*topology, s, d, copy);
    }();
    return pathDict(unwrap(std::move(result)));
}

// ---------------------------------------------------------------------------------------------------------------
// Connections (stateful, caller-owned; one session per operation sequence)
// ---------------------------------------------------------------------------------------------------------------

class ConnectionSession {
public:
    explicit ConnectionSession(const PyTopology& t) : topology_(t.topology), failures_(*topology_), manager_(*topology_) {
        manager_.setAvailability([this](const FiberLink& link) { return failures_.isLinkUsable(link); });
    }
    ConnectionSession(const ConnectionSession&) = delete;
    ConnectionSession& operator=(const ConnectionSession&) = delete;

    py::dict establish(std::int64_t id, std::int64_t source, std::int64_t destination, std::int64_t demand,
                       const RoutingConstraints& constraints) {
        ConnectionRequest request{connId(id), nodeId(source), nodeId(destination), toId(demand, "demand"), constraints};
        return connectionDict(unwrap(manager_.establish(request)));
    }
    void release(std::int64_t id) { check(manager_.release(connId(id))); }

    py::dict connection(std::int64_t id) const {
        const Connection* c = manager_.find(connId(id));
        if (c == nullptr) raise(ErrorCode::NotFound, "connection " + std::to_string(id) + " is not active");
        return connectionDict(*c);
    }
    py::list connections() const {
        py::list out;
        for (const auto& [id, c] : manager_.activeConnections()) out.append(connectionDict(c));
        return out;
    }
    bool contains(std::int64_t id) const { return manager_.contains(connId(id)); }
    std::size_t activeCount() const { return manager_.activeCount(); }

    py::dict usage(std::int64_t link) const {
        const LinkId id = linkId(link);
        return usageDict(id, unwrap(manager_.resources().usage(id)));
    }
    py::list usages() const {
        py::list out;
        for (const auto& [id, u] : manager_.resources().usages()) out.append(usageDict(id, u));
        return out;
    }
    py::dict totals() const {
        const auto& r = manager_.resources();
        py::dict d;
        d["capacity"] = r.totalCapacity();
        d["allocated"] = r.totalAllocated();
        d["available"] = r.totalAvailable();
        d["active_connections"] = manager_.activeCount();
        return d;
    }

    // Failure overlay (non-destructive). Connections already using a resource stay established at this layer; the
    // simulation engine is what re-routes or drops them.
    void failLink(std::int64_t id) { check(failures_.failLink(linkId(id))); }
    void recoverLink(std::int64_t id) { check(failures_.recoverLink(linkId(id))); }
    void failNode(std::int64_t id) { check(failures_.failNode(nodeId(id))); }
    void recoverNode(std::int64_t id) { check(failures_.recoverNode(nodeId(id))); }
    std::vector<std::int64_t> failedLinks() const { return sortedValues(failures_.failedLinks()); }
    std::vector<std::int64_t> failedNodes() const { return sortedValues(failures_.failedNodes()); }

    py::dict validate() const {
        const ValidationReport report = manager_.validate();
        ValidationResult invariants = checkResources(manager_.resources(), *topology_);
        invariants.merge(checkConnections(manager_, *topology_));
        py::dict d;
        d["valid"] = report.valid() && invariants.passed();
        d["issues"] = issuesList(report);
        d["invariant_failures"] = failuresList(invariants);
        return d;
    }

private:
    std::shared_ptr<const Topology> topology_;  // declared first: the members below reference *topology_
    FailureState failures_;
    ConnectionManager manager_;
};

// ---------------------------------------------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------------------------------------------

ScriptedArrival makeArrival(double time, std::int64_t id, std::int64_t source, std::int64_t destination, std::int64_t demand,
                            double lifetime, const std::optional<RoutingConstraints>& routing) {
    return ScriptedArrival{time,
                           ConnectionRequest{connId(id), nodeId(source), nodeId(destination), toId(demand, "demand"),
                                             routing.value_or(RoutingConstraints{})},
                           lifetime};
}

py::dict simulateImpl(const PyTopology& t, const SimulationConfig& config, const std::optional<std::vector<ScriptedArrival>>& arrivals) {
    const auto topology = t.topology;
    const SimulationConfig cfg = config;
    const auto script = arrivals;
    auto result = [&] {
        py::gil_scoped_release release;
        SimulationEngine engine;
        return script ? engine.runScript(*topology, *script, cfg) : engine.run(*topology, cfg);
    }();
    return resultDict(unwrap(std::move(result)));
}

py::dict aggregateDict(const StudyAggregate& a) {
    py::dict d;
    d["total_requests"] = a.totalRequests;
    d["total_accepted"] = a.totalAccepted;
    d["total_rejected"] = a.totalRejected;
    d["total_releases"] = a.totalReleases;
    d["rejections_by_reason"] = reasonsDict(a.rejectionsByReason);
    d["total_failure_events"] = a.totalFailureEvents;
    d["total_connections_affected"] = a.totalConnectionsAffected;
    d["total_connections_rerouted"] = a.totalConnectionsRerouted;
    d["total_connections_lost"] = a.totalConnectionsLost;
    d["aggregate_recovery_success_rate"] = a.aggregateRecoverySuccessRate;
    d["total_connection_downtime"] = a.totalConnectionDowntime;
    d["aggregate_acceptance_rate"] = a.aggregateAcceptanceRate;
    d["aggregate_blocking_rate"] = a.aggregateBlockingRate;
    d["mean_active_connections_at_end"] = a.meanActiveConnectionsAtEnd;
    d["mean_peak_active_connections"] = a.meanPeakActiveConnections;
    d["mean_peak_allocated_channels"] = a.meanPeakAllocatedChannels;
    d["mean_final_allocated_channels"] = a.meanFinalAllocatedChannels;
    d["mean_average_utilization"] = a.meanAverageUtilization;
    d["average_path_cost"] = a.averagePathCost;
    d["average_hop_count"] = a.averageHopCount;
    d["acceptance_rate"] = statsDict(a.acceptanceRate);
    d["blocking_rate"] = statsDict(a.blockingRate);
    d["average_utilization"] = statsDict(a.averageUtilization);
    return d;
}

py::dict studyImpl(const PyTopology& t, const SimulationConfig& config, std::int64_t replications, std::uint64_t baseSeed,
                   std::int64_t workers) {
    if (replications < 1) raise(ErrorCode::InvalidArgument, "replications must be at least 1");
    if (workers < 0) raise(ErrorCode::InvalidArgument, "workers must not be negative (0 = automatic)");
    const auto topology = t.topology;
    const SimulationConfig cfg = config;
    StudyConfig study;
    study.replications = static_cast<std::uint64_t>(replications);
    study.workerCount = static_cast<std::size_t>(workers);
    study.baseSeed = baseSeed;
    // The C++ SimulationStudy owns the thread pool; Python never creates threads for replications.
    auto result = [&] {
        py::gil_scoped_release release;
        return SimulationStudy::run(*topology, cfg, study);
    }();
    StudyResult r = unwrap(std::move(result));
    py::dict d;
    d["base_seed"] = r.baseSeed;
    d["workers_used"] = r.workersUsed;
    py::list reps;
    std::uint64_t index = 0;
    for (const auto& rep : r.replications) {
        py::dict e = resultDict(rep);
        e["index"] = index++;
        reps.append(e);
    }
    d["replications"] = reps;
    d["aggregate"] = aggregateDict(r.aggregate);
    return d;
}

py::dict validateSimulationImpl(const PyTopology& t, const SimulationConfig& config,
                                const std::optional<std::vector<ScriptedArrival>>& arrivals) {
    const auto topology = t.topology;
    SimulationConfig cfg = config;
    cfg.recordTrace = true;
    const auto script = arrivals;
    SimulationResult sim;
    ValidationResult validation;
    {
        auto result = [&] {
            py::gil_scoped_release release;
            SimulationEngine engine;
            return script ? engine.runScript(*topology, *script, cfg) : engine.run(*topology, cfg);
        }();
        sim = unwrap(std::move(result));
        py::gil_scoped_release release;
        validation = checkSimulationTrace(*topology, sim);
    }
    py::dict d;
    d["passed"] = validation.passed();
    d["failures"] = failuresList(validation);
    d["trace_entries"] = sim.trace.size();
    d["metrics"] = metricsDict(sim.metrics);
    return d;
}

}  // namespace

PYBIND11_MODULE(_opticalnet, m) {
    m.doc() = "OpticalNet C++ simulation engine (pybind11 bindings)";

    // ---- exceptions ----
    g_exceptions = new std::map<ErrorCode, py::object>();
    py::object base = py::reinterpret_steal<py::object>(PyErr_NewException("opticalnet._opticalnet.OpticalNetError", PyExc_Exception, nullptr));
    m.attr("OpticalNetError") = base;
    PyObject_SetAttrString(base.ptr(), "code", py::str("OPTICALNET_ERROR").ptr());
    struct Entry { ErrorCode code; const char* name; };
    static constexpr Entry entries[] = {
        {ErrorCode::InvalidArgument, "InvalidArgumentError"},     {ErrorCode::DuplicateId, "DuplicateIdError"},
        {ErrorCode::NotFound, "NotFoundError"},                   {ErrorCode::ConstraintViolation, "ConstraintViolationError"},
        {ErrorCode::ParseError, "ParseError"},                    {ErrorCode::NoRoute, "NoRouteError"},
        {ErrorCode::NoFeasibleRoute, "NoFeasibleRouteError"},     {ErrorCode::InsufficientCapacity, "InsufficientCapacityError"},
        {ErrorCode::InconsistentState, "InconsistentStateError"}, {ErrorCode::InternalError, "InternalError"},
        {ErrorCode::InvalidTransition, "InvalidTransitionError"}, {ErrorCode::ResourceUnavailable, "ResourceUnavailableError"},
    };
    for (const auto& e : entries) {
        const std::string qualified = std::string("opticalnet._opticalnet.") + e.name;
        py::object cls = py::reinterpret_steal<py::object>(PyErr_NewException(qualified.c_str(), base.ptr(), nullptr));
        PyObject_SetAttrString(cls.ptr(), "code", py::str(codeName(e.code)).ptr());
        m.attr(e.name) = cls;
        (*g_exceptions)[e.code] = cls;
    }
    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) std::rethrow_exception(p);
        } catch (const OpticalNetException& e) {
            PyErr_SetString((*g_exceptions).at(e.code()).ptr(), e.what());
        }
    });

    // ---- routing constraints ----
    py::class_<RoutingConstraints>(m, "RoutingConstraints")
        .def(py::init([](const std::string& metric, std::optional<double> maxCost, std::optional<std::int64_t> maxHops,
                         std::int64_t minLinkCapacity, const std::vector<std::int64_t>& blockedNodes,
                         const std::vector<std::int64_t>& blockedLinks) {
                 return makeConstraints(metric, maxCost, maxHops, minLinkCapacity, blockedNodes, blockedLinks);
             }),
             py::arg("metric") = "administrative", py::arg("max_cost") = std::nullopt, py::arg("max_hops") = std::nullopt,
             py::arg("min_link_capacity") = 0, py::arg("blocked_nodes") = std::vector<std::int64_t>{},
             py::arg("blocked_links") = std::vector<std::int64_t>{})
        .def_property(
            "metric", [](const RoutingConstraints& c) { return std::string(metricName(c.metric)); },
            [](RoutingConstraints& c, const std::string& s) { c.metric = parseMetric(s); })
        .def_property(
            "max_cost", [](const RoutingConstraints& c) { return c.maxCost; },
            [](RoutingConstraints& c, std::optional<double> v) { c.maxCost = v; })
        .def_property(
            "max_hops", [](const RoutingConstraints& c) { return c.maxHops; },
            [](RoutingConstraints& c, std::optional<std::int64_t> v) {
                c.maxHops = v ? std::optional<std::uint32_t>(toId(*v, "max_hops")) : std::nullopt;
            })
        .def_property(
            "min_link_capacity", [](const RoutingConstraints& c) { return c.minLinkCapacityChannels; },
            [](RoutingConstraints& c, std::int64_t v) { c.minLinkCapacityChannels = toId(v, "min_link_capacity"); })
        .def_property(
            "blocked_nodes", [](const RoutingConstraints& c) { return sortedValues(c.blockedNodes); },
            [](RoutingConstraints& c, const std::vector<std::int64_t>& v) { c.blockedNodes = nodeSet(v); })
        .def_property(
            "blocked_links", [](const RoutingConstraints& c) { return sortedValues(c.blockedLinks); },
            [](RoutingConstraints& c, const std::vector<std::int64_t>& v) { c.blockedLinks = linkSet(v); });

    // ---- topology ----
    py::class_<PyTopology>(m, "Topology", "Immutable network topology. Safe to share between threads.")
        .def_static("from_json", &topologyFromJson, py::arg("text"), "Parse the strict JSON topology schema (C++ is authoritative).")
        .def_static("from_file", &topologyFromFile, py::arg("path"))
        .def("to_json", [](const PyTopology& t, int indent) { return topologyToJson(*t.topology, indent); }, py::arg("indent") = 2)
        .def_property_readonly("node_count", [](const PyTopology& t) { return t.topology->nodeCount(); })
        .def_property_readonly("link_count", [](const PyTopology& t) { return t.topology->linkCount(); })
        .def("nodes", [](const PyTopology& t) {
            py::list out;
            for (const auto& [id, n] : t.topology->network().nodes()) out.append(nodeDict(n));
            return out;
        })
        .def("links", [](const PyTopology& t) {
            py::list out;
            for (const auto& [id, l] : t.topology->network().links()) out.append(linkDict(l));
            return out;
        })
        .def("transceivers", [](const PyTopology& t) {
            py::list out;
            for (const auto& [id, x] : t.topology->network().transceivers()) {
                py::dict d;
                d["id"] = id.value();
                d["name"] = x.name();
                d["data_rate_gbps"] = x.dataRateGbps();
                d["reach_km"] = x.reachKm();
                out.append(d);
            }
            return out;
        })
        .def("switching_elements", [](const PyTopology& t) {
            py::list out;
            for (const auto& [id, x] : t.topology->network().switchingElements()) {
                py::dict d;
                d["id"] = id.value();
                d["name"] = x.name();
                d["type"] = std::string(toString(x.type()));
                d["ports"] = x.portCount();
                out.append(d);
            }
            return out;
        })
        .def("node", [](const PyTopology& t, std::int64_t id) {
            const Node* n = t.topology->findNode(nodeId(id));
            if (n == nullptr) raise(ErrorCode::NotFound, "node " + std::to_string(id) + " does not exist");
            return nodeDict(*n);
        }, py::arg("id"))
        .def("link", [](const PyTopology& t, std::int64_t id) {
            const FiberLink* l = t.topology->findLink(linkId(id));
            if (l == nullptr) raise(ErrorCode::NotFound, "link " + std::to_string(id) + " does not exist");
            return linkDict(*l);
        }, py::arg("id"))
        .def("neighbors", [](const PyTopology& t, std::int64_t id) {
            if (!t.topology->hasNode(nodeId(id))) raise(ErrorCode::NotFound, "node " + std::to_string(id) + " does not exist");
            return values(t.topology->neighbors(nodeId(id)));
        }, py::arg("node"))
        .def("links_between", [](const PyTopology& t, std::int64_t a, std::int64_t b) {
            if (!t.topology->hasNode(nodeId(a))) raise(ErrorCode::NotFound, "node " + std::to_string(a) + " does not exist");
            if (!t.topology->hasNode(nodeId(b))) raise(ErrorCode::NotFound, "node " + std::to_string(b) + " does not exist");
            return values(t.topology->linksBetween(nodeId(a), nodeId(b)));
        }, py::arg("source"), py::arg("target"))
        .def("is_reachable", [](const PyTopology& t, std::int64_t a, std::int64_t b) {
            if (!t.topology->hasNode(nodeId(a))) raise(ErrorCode::NotFound, "node " + std::to_string(a) + " does not exist");
            if (!t.topology->hasNode(nodeId(b))) raise(ErrorCode::NotFound, "node " + std::to_string(b) + " does not exist");
            return t.topology->isReachable(nodeId(a), nodeId(b));
        }, py::arg("source"), py::arg("target"))
        .def("is_connected", [](const PyTopology& t) { return t.topology->isConnected(); })
        .def("is_strongly_connected", [](const PyTopology& t) { return t.topology->isStronglyConnected(); })
        .def("connected_components", [](const PyTopology& t) {
            std::vector<std::vector<std::uint32_t>> out;
            for (const auto& c : t.topology->connectedComponents()) out.push_back(values(c));
            return out;
        })
        .def("validate", [](const PyTopology& t) { return topologyValidate(*t.topology); });

    // ---- connection session ----
    py::class_<ConnectionSession>(m, "ConnectionSession",
                                  "Mutable connection/resource state over one topology. Not thread-safe: use one per operation sequence.")
        .def(py::init<const PyTopology&>(), py::arg("topology"), py::keep_alive<1, 2>())
        .def("establish", &ConnectionSession::establish, py::arg("id"), py::arg("source"), py::arg("destination"),
             py::arg("demand") = 1, py::arg("constraints") = RoutingConstraints{})
        .def("release", &ConnectionSession::release, py::arg("id"))
        .def("connection", &ConnectionSession::connection, py::arg("id"))
        .def("connections", &ConnectionSession::connections)
        .def("contains", &ConnectionSession::contains, py::arg("id"))
        .def_property_readonly("active_count", &ConnectionSession::activeCount)
        .def("usage", &ConnectionSession::usage, py::arg("link"))
        .def("usages", &ConnectionSession::usages)
        .def("totals", &ConnectionSession::totals)
        .def("fail_link", &ConnectionSession::failLink, py::arg("id"))
        .def("recover_link", &ConnectionSession::recoverLink, py::arg("id"))
        .def("fail_node", &ConnectionSession::failNode, py::arg("id"))
        .def("recover_node", &ConnectionSession::recoverNode, py::arg("id"))
        .def("failed_links", &ConnectionSession::failedLinks)
        .def("failed_nodes", &ConnectionSession::failedNodes)
        .def("validate", &ConnectionSession::validate);

    // ---- simulation configuration ----
    py::class_<RandomFailureConfig>(m, "RandomFailureConfig")
        .def(py::init([](double rate, double repair, double nodeProbability) {
                 return RandomFailureConfig{rate, repair, nodeProbability};
             }),
             py::arg("failure_rate") = 0.0, py::arg("mean_repair_time") = 10.0, py::arg("node_failure_probability") = 0.0)
        .def_readwrite("failure_rate", &RandomFailureConfig::failureRate)
        .def_readwrite("mean_repair_time", &RandomFailureConfig::meanRepairTime)
        .def_readwrite("node_failure_probability", &RandomFailureConfig::nodeFailureProbability);

    py::class_<ScriptedFailure>(m, "ScriptedFailure")
        .def(py::init([](double time, const std::string& action, std::int64_t id) {
                 return ScriptedFailure{time, parseAction(action), toId(id, "failure resource id")};
             }),
             py::arg("time"), py::arg("action"), py::arg("id"))
        .def_property_readonly("time", [](const ScriptedFailure& f) { return f.time; })
        .def_property_readonly("action", [](const ScriptedFailure& f) { return std::string(actionName(f.action)); })
        .def_property_readonly("id", [](const ScriptedFailure& f) { return f.id; });

    py::class_<SimulationConfig>(m, "SimulationConfig")
        .def(py::init<>())
        .def_readwrite("seed", &SimulationConfig::seed)
        .def_readwrite("end_time", &SimulationConfig::endTime)
        .def_readwrite("max_requests", &SimulationConfig::maxRequests)
        .def_readwrite("arrival_rate", &SimulationConfig::arrivalRate)
        .def_property(
            "arrival_timing", [](const SimulationConfig& c) { return std::string(timingName(c.arrivalTiming)); },
            [](SimulationConfig& c, const std::string& s) { c.arrivalTiming = parseTiming(s); })
        .def_readwrite("mean_lifetime", &SimulationConfig::meanLifetime)
        .def_property(
            "lifetime_timing", [](const SimulationConfig& c) { return std::string(timingName(c.lifetimeTiming)); },
            [](SimulationConfig& c, const std::string& s) { c.lifetimeTiming = parseTiming(s); })
        .def_readwrite("min_demand", &SimulationConfig::minCapacityChannels)
        .def_readwrite("max_demand", &SimulationConfig::maxCapacityChannels)
        .def_readwrite("routing", &SimulationConfig::routing)
        .def_readwrite("failure_schedule", &SimulationConfig::failureSchedule)
        .def_readwrite("random_failures", &SimulationConfig::randomFailures)
        .def_property(
            "recovery_policy", [](const SimulationConfig& c) { return std::string(policyName(c.recoveryPolicy)); },
            [](SimulationConfig& c, const std::string& s) { c.recoveryPolicy = parsePolicy(s); })
        .def_readwrite("record_trace", &SimulationConfig::recordTrace);

    py::class_<ScriptedArrival>(m, "ScriptedArrival")
        .def(py::init([](double time, std::int64_t id, std::int64_t source, std::int64_t destination, std::int64_t demand,
                         double lifetime, std::optional<RoutingConstraints> routing) {
                 return makeArrival(time, id, source, destination, demand, lifetime, routing);
             }),
             py::arg("time"), py::arg("connection_id"), py::arg("source"), py::arg("destination"), py::arg("demand") = 1,
             py::arg("lifetime") = 1.0, py::arg("routing") = std::nullopt);

    // ---- functions ----
    m.def("load_topology", &topologyFromFile, py::arg("path"), "Load a topology from a JSON file.");
    m.def("topology_from_json", &topologyFromJson, py::arg("text"));
    m.def("route", [](const PyTopology& t, std::int64_t source, std::int64_t destination, const std::string& metric,
                      std::optional<double> maxCost, std::optional<std::int64_t> maxHops, std::int64_t minLinkCapacity,
                      const std::vector<std::int64_t>& blockedNodes, const std::vector<std::int64_t>& blockedLinks) {
              return routeImpl(t, source, destination,
                               makeConstraints(metric, maxCost, maxHops, minLinkCapacity, blockedNodes, blockedLinks));
          },
          py::arg("topology"), py::arg("source"), py::arg("destination"), py::arg("metric") = "administrative",
          py::arg("max_cost") = std::nullopt, py::arg("max_hops") = std::nullopt, py::arg("min_link_capacity") = 0,
          py::arg("blocked_nodes") = std::vector<std::int64_t>{}, py::arg("blocked_links") = std::vector<std::int64_t>{});
    m.def("route_with", &routeImpl, py::arg("topology"), py::arg("source"), py::arg("destination"), py::arg("constraints"));
    m.def("simulate", &simulateImpl, py::arg("topology"), py::arg("config"), py::arg("arrivals") = std::nullopt,
          "Run one simulation (generated workload, or the scripted arrivals when given).");
    m.def("run_study", &studyImpl, py::arg("topology"), py::arg("config"), py::arg("replications"), py::arg("base_seed") = 1,
          py::arg("workers") = 0, "Run independent replications on the C++ thread pool.");
    m.def("validate_topology", [](const PyTopology& t) { return topologyValidate(*t.topology); }, py::arg("topology"));
    m.def("validate_simulation", &validateSimulationImpl, py::arg("topology"), py::arg("config"), py::arg("arrivals") = std::nullopt,
          "Run a simulation with a trace and check it with the independent C++ trace validator.");
}

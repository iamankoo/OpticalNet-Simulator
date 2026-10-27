#include "opticalnet/validation/Invariants.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace opticalnet {

namespace {

using Edge = std::pair<std::uint32_t, std::uint32_t>;  // (link id, neighbour node id)

std::string n(NodeId id) { return "node " + std::to_string(id.value()); }
std::string l(LinkId id) { return "link " + std::to_string(id.value()); }
std::string c(ConnectionId id) { return "connection " + std::to_string(id.value()); }

// Deliberately written out here rather than reusing linkCost(): the validator must not trust the code it validates.
double metricValue(const FiberLink& link, CostMetric metric) {
    switch (metric) {
        case CostMetric::HopCount: return 1.0;
        case CostMetric::Distance: return link.lengthKm();
        case CostMetric::Administrative: return link.administrativeCost();
    }
    return 0.0;
}

// The documented order of events that share a timestamp: link recovery, node recovery, link failure, node failure,
// connection release, connection arrival. Encoded here independently of the EventType numbering.
int sameTimePriority(EventType type) {
    switch (type) {
        case EventType::LinkRecovery: return 0;
        case EventType::NodeRecovery: return 1;
        case EventType::LinkFailure: return 2;
        case EventType::NodeFailure: return 3;
        case EventType::ConnectionRelease: return 4;
        case EventType::ConnectionArrival: return 5;
    }
    return -1;
}

bool close(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max({1.0, std::fabs(a), std::fabs(b)}); }

std::vector<Edge> asEdges(std::span<const Adjacency> list) {
    std::vector<Edge> edges;
    for (const Adjacency& a : list) edges.emplace_back(a.link.value(), a.neighbor.value());
    return edges;
}

}  // namespace

// ================================================================================================ topology

ValidationResult checkTopology(const Topology& topology, bool checkPorts) {
    ValidationResult r;
    const Network& net = topology.network();

    std::set<std::uint32_t> nodeIds, linkIds;
    for (const auto& [key, node] : net.nodes()) {
        if (key != node.id()) r.fail("topology.node_key", n(node.id()) + " is stored under key " + std::to_string(key.value()));
        if (!nodeIds.insert(node.id().value()).second) r.fail("topology.unique_node_ids", "duplicate " + n(node.id()));
    }
    std::map<NodeId, std::set<Edge>> expectedOut, expectedIn;
    for (const auto& [key, link] : net.links()) {
        if (key != link.id()) r.fail("topology.link_key", l(link.id()) + " is stored under key " + std::to_string(key.value()));
        if (!linkIds.insert(link.id().value()).second) r.fail("topology.unique_link_ids", "duplicate " + l(link.id()));
        if (!net.findNode(link.source())) r.fail("topology.link_endpoints_exist", l(link.id()) + " references missing " + n(link.source()));
        if (!net.findNode(link.target())) r.fail("topology.link_endpoints_exist", l(link.id()) + " references missing " + n(link.target()));
        if (link.source() == link.target()) r.fail("topology.no_self_links", l(link.id()) + " is a self-link on " + n(link.source()));
        if (!(link.capacityChannels() > 0)) r.fail("topology.link_capacity_positive", l(link.id()) + " has no capacity");
        if (!(link.lengthKm() > 0.0) || !(link.administrativeCost() > 0.0)) r.fail("topology.link_weights_positive", l(link.id()) + " has a non-positive length or cost");

        const std::uint32_t id = link.id().value();
        expectedIn[link.source()].insert({id, link.target().value()});
        expectedIn[link.target()].insert({id, link.source().value()});
        expectedOut[link.source()].insert({id, link.target().value()});                       // source -> target always
        if (link.direction() == LinkDirection::Bidirectional) expectedOut[link.target()].insert({id, link.source().value()});
    }

    // Adjacency recomputed from the link list must equal what the topology exposes, in LinkId order, without duplicates.
    for (const auto& [nodeId, node] : net.nodes()) {
        const std::vector<Edge> want(expectedOut[nodeId].begin(), expectedOut[nodeId].end());
        const std::vector<Edge> got = asEdges(topology.outgoing(nodeId));
        if (got != want) r.fail("topology.outgoing_adjacency", n(nodeId) + ": outgoing list has " + std::to_string(got.size()) + " entries, expected " + std::to_string(want.size()));
        const std::vector<Edge> wantIn(expectedIn[nodeId].begin(), expectedIn[nodeId].end());
        const std::vector<Edge> gotIn = asEdges(topology.incident(nodeId));
        if (gotIn != wantIn) r.fail("topology.incident_adjacency", n(nodeId) + ": incident list has " + std::to_string(gotIn.size()) + " entries, expected " + std::to_string(wantIn.size()));
        if (topology.degree(nodeId) != wantIn.size()) r.fail("topology.degree", n(nodeId) + ": degree " + std::to_string(topology.degree(nodeId)) + " expected " + std::to_string(wantIn.size()));
    }
    // No adjacency entries may exist for nodes that are not in the network.
    for (const auto& [nodeId, edges] : expectedOut) {
        if (!net.findNode(nodeId) && !edges.empty()) r.fail("topology.adjacency_of_missing_node", "links reference " + n(nodeId) + " which does not exist");
    }

    // Equipment relationships.
    std::set<std::uint32_t> usedSwitches, usedTransceivers;
    for (const auto& [nodeId, node] : net.nodes()) {
        if (node.switchingElement()) {
            const auto sw = node.switchingElement()->value();
            if (!net.findSwitchingElement(*node.switchingElement())) r.fail("topology.equipment_exists", n(nodeId) + " references missing switching element " + std::to_string(sw));
            if (!usedSwitches.insert(sw).second) r.fail("topology.equipment_unique", "switching element " + std::to_string(sw) + " is attached to more than one node");
            if (checkPorts) {
                if (const SwitchingElement* element = net.findSwitchingElement(*node.switchingElement())) {
                    if (expectedIn[nodeId].size() > element->portCount())
                        r.fail("topology.port_constraint", n(nodeId) + " has " + std::to_string(expectedIn[nodeId].size()) + " links but its switch has " + std::to_string(element->portCount()) + " ports");
                }
            }
        }
        for (const TransceiverId t : node.transceivers()) {
            if (!net.findTransceiver(t)) r.fail("topology.equipment_exists", n(nodeId) + " references missing transceiver " + std::to_string(t.value()));
            if (!usedTransceivers.insert(t.value()).second) r.fail("topology.equipment_unique", "transceiver " + std::to_string(t.value()) + " is attached more than once");
        }
    }
    return r;
}

// ================================================================================================ failures

ValidationResult checkFailureState(const FailureState& failures, const Topology& topology) {
    ValidationResult r;
    for (const LinkId id : failures.failedLinks())
        if (!topology.hasLink(id)) r.fail("failures.failed_link_exists", "failed " + l(id) + " is not in the topology");
    for (const NodeId id : failures.failedNodes())
        if (!topology.hasNode(id)) r.fail("failures.failed_node_exists", "failed " + n(id) + " is not in the topology");
    for (const auto& [id, link] : topology.network().links()) {
        const bool shouldBeDown = failures.failedLinks().contains(id) || failures.failedNodes().contains(link.source()) ||
                                  failures.failedNodes().contains(link.target());
        if (failures.isLinkUsable(link) == shouldBeDown)
            r.fail("failures.availability", l(id) + (shouldBeDown ? " should be unavailable but is usable" : " should be available but is not usable"));
    }
    return r;
}

// ================================================================================================ routing

ValidationResult checkPath(const Topology& topology, const Path& path, const PathExpectation& e) {
    ValidationResult r;
    const std::string who = "path " + std::to_string(e.source.value()) + "->" + std::to_string(e.destination.value());
    const auto& nodes = path.nodes();
    const auto& links = path.links();

    if (nodes.empty() || nodes.size() != links.size() + 1) {
        r.fail("path.shape", who + ": " + std::to_string(nodes.size()) + " nodes and " + std::to_string(links.size()) + " links");
        return r;
    }
    if (nodes.front() != e.source) r.fail("path.starts_at_source", who + " starts at " + n(nodes.front()));
    if (nodes.back() != e.destination) r.fail("path.ends_at_destination", who + " ends at " + n(nodes.back()));
    if (path.hopCount() != links.size()) r.fail("path.hop_count", who + ": hopCount() " + std::to_string(path.hopCount()) + " but " + std::to_string(links.size()) + " links");
    if (path.metric() != e.metric) r.fail("path.metric", who + " is expressed in a different metric than requested");
    if (std::set<NodeId>(nodes.begin(), nodes.end()).size() != nodes.size()) r.fail("path.simple", who + " visits a node twice");

    double cost = 0.0;
    for (std::size_t i = 0; i < links.size(); ++i) {
        const FiberLink* link = topology.findLink(links[i]);
        if (!link) {
            r.fail("path.links_exist", who + " uses missing " + l(links[i]));
            continue;
        }
        const NodeId from = nodes[i], to = nodes[i + 1];
        const bool forward = link->source() == from && link->target() == to;
        const bool backward = link->source() == to && link->target() == from && link->direction() == LinkDirection::Bidirectional;
        if (!forward && !backward) r.fail("path.links_connect_consecutive_nodes", who + ": " + l(links[i]) + " cannot carry traffic from " + n(from) + " to " + n(to));
        cost += metricValue(*link, e.metric);

        if (e.failures) {
            if (e.failures->isLinkFailed(links[i])) r.fail("path.avoids_failed_links", who + " uses failed " + l(links[i]));
        }
        if (e.constraints) {
            const RoutingConstraints& k = *e.constraints;
            if (k.blockedLinks.contains(links[i])) r.fail("path.avoids_blocked_links", who + " uses blocked " + l(links[i]));
            if (link->capacityChannels() < k.minLinkCapacityChannels) r.fail("path.link_capacity", who + ": " + l(links[i]) + " has capacity " + std::to_string(link->capacityChannels()) + " < " + std::to_string(k.minLinkCapacityChannels));
            if (k.linkFilter && !k.linkFilter(*link)) r.fail("path.link_filter", who + ": " + l(links[i]) + " is vetoed by the link filter");
        }
    }
    if (e.failures) {
        for (const NodeId node : nodes)
            if (e.failures->isNodeFailed(node)) r.fail("path.avoids_failed_nodes", who + " passes through failed " + n(node));
    }
    if (!close(cost, path.cost())) r.fail("path.cost", who + ": cost() is " + std::to_string(path.cost()) + " but the selected metric sums to " + std::to_string(cost));
    if (e.constraints) {
        const RoutingConstraints& k = *e.constraints;
        if (k.maxHops && links.size() > *k.maxHops) r.fail("path.max_hops", who + " has " + std::to_string(links.size()) + " hops, limit " + std::to_string(*k.maxHops));
        if (k.maxCost && cost > *k.maxCost + 1e-9) r.fail("path.max_cost", who + " costs " + std::to_string(cost) + ", limit " + std::to_string(*k.maxCost));
        for (const NodeId node : nodes)
            if (k.blockedNodes.contains(node)) r.fail("path.avoids_blocked_nodes", who + " passes through blocked " + n(node));
    }
    return r;
}

// ================================================================================================ resources

ValidationResult checkResources(const ResourceManager& resources, const Topology& topology) {
    ValidationResult r;
    std::uint64_t capacity = 0, allocated = 0;
    for (const auto& [id, link] : topology.network().links()) {
        capacity += link.capacityChannels();
        const std::uint64_t used = resources.allocated(id);
        allocated += used;
        if (used > link.capacityChannels()) r.fail("resources.link_within_capacity", l(id) + " has " + std::to_string(used) + " allocated of " + std::to_string(link.capacityChannels()));
        const auto usage = resources.usage(id);
        if (!usage.ok()) {
            r.fail("resources.usage_available", l(id) + ": usage() failed: " + usage.error().describe());
            continue;
        }
        if (usage.value().total != link.capacityChannels()) r.fail("resources.total_matches_topology", l(id) + ": usage().total differs from the configured capacity");
        if (usage.value().allocated != used) r.fail("resources.usage_matches_allocated", l(id) + ": usage().allocated differs from allocated()");
        if (static_cast<std::uint64_t>(usage.value().allocated) + usage.value().available != usage.value().total)
            r.fail("resources.allocated_plus_available", l(id) + ": allocated + available != total");
    }
    for (const auto& [id, channels] : resources.allocations())
        if (!topology.hasLink(id)) r.fail("resources.no_allocation_on_unknown_link", std::to_string(channels) + " channels allocated on missing " + l(id));
    if (resources.totalCapacity() != capacity) r.fail("resources.total_capacity", "totalCapacity() " + std::to_string(resources.totalCapacity()) + " != sum " + std::to_string(capacity));
    if (resources.totalAllocated() != allocated) r.fail("resources.total_allocated", "totalAllocated() " + std::to_string(resources.totalAllocated()) + " != sum of per-link allocations " + std::to_string(allocated));
    if (capacity >= allocated && resources.totalAvailable() != capacity - allocated)
        r.fail("resources.total_available", "totalAvailable() " + std::to_string(resources.totalAvailable()) + " != capacity - allocated = " + std::to_string(capacity - allocated));
    return r;
}

// ================================================================================================ connections

ValidationResult checkConnections(const ConnectionManager& manager, const Topology& topology, const FailureState* failures) {
    ValidationResult r;
    std::map<LinkId, std::uint64_t> ledger;  // independent shadow ledger: what the active connections should hold per link
    for (const auto& [key, connection] : manager.activeConnections()) {
        const std::string who = c(key);
        if (connection.id() != key) r.fail("connections.id_matches_key", who + " is stored under a different id");
        if (connection.capacityChannels() == 0) r.fail("connections.demand_positive", who + " has zero demand");
        if (connection.capacityChannels() != connection.request().capacityChannels) r.fail("connections.demand_matches_request", who + ": demand differs from its request");
        if (connection.allocatedLinks().empty()) r.fail("connections.path_nonempty", who + " has no links");

        const PathExpectation e{connection.request().source, connection.request().destination, connection.path().metric(), nullptr, failures};
        // Only feasibility facts that hold for the whole life of the connection are checked here (the constraints were
        // satisfied when the path was chosen, and the path is stored with the connection).
        r.merge(checkPath(topology, connection.path(), e));
        if (failures) {
            for (const LinkId id : connection.allocatedLinks()) {
                const FiberLink* link = topology.findLink(id);
                if (link && !failures->isLinkUsable(*link)) r.fail("connections.active_on_failed_resource", who + " is active on unavailable " + l(id));
            }
        }
        for (const LinkId id : connection.allocatedLinks()) ledger[id] += connection.capacityChannels();
    }
    for (const auto& [id, link] : topology.network().links()) {
        const std::uint64_t expected = ledger.contains(id) ? ledger.at(id) : 0;
        const std::uint64_t actual = manager.resources().allocated(id);
        if (expected != actual) r.fail("connections.allocation_matches_ledger", l(id) + ": active connections hold " + std::to_string(expected) + " channels but " + std::to_string(actual) + " are allocated");
    }
    for (const auto& [id, channels] : ledger)
        if (!topology.hasLink(id)) r.fail("connections.links_exist", "an active connection uses missing " + l(id));
    return r;
}

// ================================================================================================ simulation

ValidationResult checkSimulationTrace(const Topology& topology, const SimulationResult& result) {
    ValidationResult r;
    const SimulationMetrics& m = result.metrics;
    if (result.trace.empty() && m.eventsProcessed > 0) {
        r.fail("trace.recorded", "the result has no trace; run with recordTrace to validate it");
        return r;
    }

    std::uint64_t capacity = 0;
    for (const auto& [id, link] : topology.network().links()) capacity += link.capacityChannels();

    struct Held {
        std::uint64_t channelLinks;
        bool pending;  // detached by a failure, awaiting its Rerouted/Lost entry
    };
    std::map<ConnectionId, Held> held;
    std::uint64_t allocated = 0, active = 0, peakAllocated = 0, peakActive = 0;
    std::uint64_t requests = 0, accepted = 0, rejected = 0, releases = 0;
    std::uint64_t linkFail = 0, nodeFail = 0, linkRec = 0, nodeRec = 0, affected = 0, rerouted = 0, lost = 0, eventEntries = 0;
    std::map<ErrorCode, std::uint64_t> reasons;
    std::uint64_t sumHops = 0, maxHops = 0;
    double integral = 0.0, previous = 0.0;
    bool groupOpen = false;
    bool haveLastEvent = false;
    double lastEventTime = 0.0;
    int lastEventPriority = 0;

    const auto closeGroup = [&] {
        if (!groupOpen) return;
        peakAllocated = std::max(peakAllocated, allocated);
        peakActive = std::max(peakActive, active);
        if (allocated > capacity) r.fail("trace.within_capacity", "allocation " + std::to_string(allocated) + " exceeds total capacity " + std::to_string(capacity));
        groupOpen = false;
    };

    for (std::size_t i = 0; i < result.trace.size(); ++i) {
        const TraceEntry& e = result.trace[i];
        const std::string at = "trace entry " + std::to_string(i) + " (t=" + std::to_string(e.time) + ")";
        if (e.time < previous) r.fail("trace.time_monotonic", at + " goes back in time from " + std::to_string(previous));
        integral += static_cast<double>(allocated) * (e.time - previous);
        previous = std::max(previous, e.time);
        const std::uint64_t channelLinks = static_cast<std::uint64_t>(e.capacityChannels) * e.hops;

        switch (e.kind) {
            case TraceKind::Event:
                closeGroup();
                groupOpen = true;
                ++eventEntries;
                if (haveLastEvent && e.time == lastEventTime && sameTimePriority(e.type) < lastEventPriority)
                    r.fail("trace.same_time_order", at + ": an event of lower priority follows a higher-priority one at the same time (order must be recoveries, failures, releases, arrivals)");
                haveLastEvent = true;
                lastEventTime = e.time;
                lastEventPriority = sameTimePriority(e.type);
                switch (e.type) {
                    case EventType::ConnectionArrival:
                        ++requests;
                        if (e.success) {
                            ++accepted;
                            if (held.contains(e.connection)) r.fail("trace.unique_active_ids", at + ": " + c(e.connection) + " accepted while already active");
                            if (e.hops == 0) r.fail("trace.accepted_has_path", at + ": " + c(e.connection) + " accepted with 0 hops");
                            held[e.connection] = {channelLinks, false};
                            allocated += channelLinks;
                            ++active;
                            sumHops += e.hops;
                            maxHops = std::max<std::uint64_t>(maxHops, e.hops);
                        } else {
                            ++rejected;
                            if (!e.reason) r.fail("trace.rejection_has_reason", at + ": rejected without a reason");
                            else ++reasons[*e.reason];
                        }
                        break;
                    case EventType::ConnectionRelease: {
                        const auto it = held.find(e.connection);
                        if (it == held.end() || it->second.pending) {
                            r.fail("trace.release_of_active", at + ": release of " + c(e.connection) + " which is not active");
                            break;
                        }
                        allocated -= it->second.channelLinks;
                        held.erase(it);
                        --active;
                        ++releases;
                        break;
                    }
                    case EventType::LinkFailure: ++linkFail; break;
                    case EventType::NodeFailure: ++nodeFail; break;
                    case EventType::LinkRecovery: ++linkRec; break;
                    case EventType::NodeRecovery: ++nodeRec; break;
                }
                break;
            case TraceKind::Affected: {
                const auto it = held.find(e.connection);
                if (it == held.end() || it->second.pending) {
                    r.fail("trace.affected_was_active", at + ": " + c(e.connection) + " reported affected but was not active");
                    break;
                }
                ++affected;
                allocated -= it->second.channelLinks;  // detached: its channels are free during recovery
                --active;
                it->second.pending = true;
                break;
            }
            case TraceKind::Rerouted: {
                const auto it = held.find(e.connection);
                if (it == held.end() || !it->second.pending) {
                    r.fail("trace.reroute_of_affected", at + ": " + c(e.connection) + " rerouted without being affected");
                    break;
                }
                ++rerouted;
                it->second = {channelLinks, false};
                allocated += channelLinks;
                ++active;
                break;
            }
            case TraceKind::Lost: {
                const auto it = held.find(e.connection);
                if (it == held.end() || !it->second.pending) {
                    r.fail("trace.loss_of_affected", at + ": " + c(e.connection) + " lost without being affected");
                    break;
                }
                ++lost;
                held.erase(it);
                break;
            }
        }
    }
    closeGroup();
    for (const auto& [id, h] : held)
        if (h.pending) r.fail("trace.affected_resolved", c(id) + " was affected but is neither rerouted nor lost");

    if (m.duration < previous) r.fail("trace.duration", "duration " + std::to_string(m.duration) + " is before the last event at " + std::to_string(previous));
    integral += static_cast<double>(allocated) * (m.duration - previous);

    const auto expectEq = [&r](const char* name, std::uint64_t got, std::uint64_t want) {
        if (got != want) r.fail(std::string("metrics.") + name, "metrics report " + std::to_string(got) + " but the trace gives " + std::to_string(want));
    };
    expectEq("totalRequests", m.totalRequests, requests);
    expectEq("acceptedRequests", m.acceptedRequests, accepted);
    expectEq("rejectedRequests", m.rejectedRequests, rejected);
    expectEq("totalReleases", m.totalReleases, releases);
    expectEq("linkFailures", m.linkFailures, linkFail);
    expectEq("nodeFailures", m.nodeFailures, nodeFail);
    expectEq("totalFailureEvents", m.totalFailureEvents, linkFail + nodeFail);
    expectEq("linkRecoveries", m.linkRecoveries, linkRec);
    expectEq("nodeRecoveries", m.nodeRecoveries, nodeRec);
    expectEq("connectionsAffected", m.connectionsAffected, affected);
    expectEq("connectionsRerouted", m.connectionsRerouted, rerouted);
    expectEq("connectionsLost", m.connectionsLost, lost);
    expectEq("failedLinksAtEnd", m.failedLinksAtEnd, linkFail - std::min(linkFail, linkRec));
    expectEq("failedNodesAtEnd", m.failedNodesAtEnd, nodeFail - std::min(nodeFail, nodeRec));
    expectEq("totalCapacityChannels", m.totalCapacityChannels, capacity);
    expectEq("finalAllocatedChannels", m.finalAllocatedChannels, allocated);
    expectEq("finalAvailableChannels", m.finalAvailableChannels, capacity - std::min(capacity, allocated));
    expectEq("activeConnectionsAtEnd", m.activeConnectionsAtEnd, active);
    expectEq("peakAllocatedChannels", m.peakAllocatedChannels, peakAllocated);
    expectEq("peakActiveConnections", m.peakActiveConnections, peakActive);
    expectEq("maxHopCount", m.maxHopCount, maxHops);
    if (requests != accepted + rejected) r.fail("trace.requests_partition", "requests != accepted + rejected");
    if (accepted != active + releases + lost) r.fail("trace.accepted_accounting", "accepted != active + released + lost");
    if (affected != rerouted + lost) r.fail("trace.affected_accounting", "affected != rerouted + lost");
    if (m.eventsProcessed < eventEntries || (lost == 0 && m.eventsProcessed != eventEntries))
        r.fail("metrics.eventsProcessed", "metrics report " + std::to_string(m.eventsProcessed) + " events, the trace shows " + std::to_string(eventEntries));
    if (m.rejectionsByReason != reasons) r.fail("metrics.rejectionsByReason", "per-reason rejection counts differ from the trace");

    const double hopMean = accepted ? static_cast<double>(sumHops) / static_cast<double>(accepted) : 0.0;
    if (!close(m.averageHopCount, hopMean)) r.fail("metrics.averageHopCount", "metrics " + std::to_string(m.averageHopCount) + " vs trace " + std::to_string(hopMean));
    const double acceptance = requests ? static_cast<double>(accepted) / static_cast<double>(requests) : 0.0;
    const double blocking = requests ? static_cast<double>(rejected) / static_cast<double>(requests) : 0.0;
    if (!close(m.acceptanceRate, acceptance)) r.fail("metrics.acceptanceRate", "metrics " + std::to_string(m.acceptanceRate) + " vs trace " + std::to_string(acceptance));
    if (!close(m.blockingRate, blocking)) r.fail("metrics.blockingRate", "metrics " + std::to_string(m.blockingRate) + " vs trace " + std::to_string(blocking));
    const double utilization = (capacity > 0 && m.duration > 0.0) ? integral / (static_cast<double>(capacity) * m.duration) : 0.0;
    if (!close(m.averageUtilization, utilization)) r.fail("metrics.averageUtilization", "metrics " + std::to_string(m.averageUtilization) + " vs time-weighted recomputation " + std::to_string(utilization));
    const double recovery = affected ? static_cast<double>(rerouted) / static_cast<double>(affected) : 0.0;
    if (!close(m.recoverySuccessRate, recovery)) r.fail("metrics.recoverySuccessRate", "metrics " + std::to_string(m.recoverySuccessRate) + " vs trace " + std::to_string(recovery));
    if (m.averageUtilization < -1e-12 || m.averageUtilization > 1.0 + 1e-9) r.fail("metrics.utilization_range", "utilization " + std::to_string(m.averageUtilization) + " outside [0, 1]");
    return r;
}

}  // namespace opticalnet

#include "opticalnet/topology/Topology.hpp"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_set>

namespace opticalnet {

namespace {

bool byLink(const Adjacency& a, const Adjacency& b) { return a.link < b.link; }

void insertSorted(std::vector<Adjacency>& list, Adjacency entry) {
    list.insert(std::ranges::upper_bound(list, entry, byLink), entry);
}

void eraseLink(std::vector<Adjacency>& list, LinkId link) {
    std::erase_if(list, [link](const Adjacency& a) { return a.link == link; });
}

}  // namespace

// ----------------------------------------------------------------- construction

Topology::Topology(Network network) : network_(std::move(network)) {
    for (const auto& entry : network_.nodes()) {
        outgoing_[entry.first];
        incident_[entry.first];
    }
    for (const auto& entry : network_.links()) indexLink(entry.second);
}

void Topology::indexLink(const FiberLink& link) {
    const Adjacency toTarget{link.target(), link.id()};
    const Adjacency toSource{link.source(), link.id()};
    insertSorted(incident_[link.source()], toTarget);
    insertSorted(incident_[link.target()], toSource);
    insertSorted(outgoing_[link.source()], toTarget);
    if (link.direction() == LinkDirection::Bidirectional) insertSorted(outgoing_[link.target()], toSource);
}

void Topology::unindexLink(const FiberLink& link) {
    eraseLink(incident_[link.source()], link.id());
    eraseLink(incident_[link.target()], link.id());
    eraseLink(outgoing_[link.source()], link.id());
    eraseLink(outgoing_[link.target()], link.id());
}

// -------------------------------------------------------------------- mutation

Result<void> Topology::addNode(Node node) {
    const NodeId id = node.id();
    auto result = network_.addNode(std::move(node));
    if (!result.ok()) return result;
    outgoing_[id];
    incident_[id];
    return {};
}

Result<void> Topology::removeNode(NodeId id) {
    auto result = network_.removeNode(id);
    if (!result.ok()) return result;
    outgoing_.erase(id);
    incident_.erase(id);
    return {};
}

Result<void> Topology::addLink(FiberLink link) {
    auto result = network_.addLink(link);  // validates id uniqueness and endpoints
    if (!result.ok()) return result;
    indexLink(link);
    return {};
}

Result<void> Topology::removeLink(LinkId id) {
    const FiberLink* link = network_.findLink(id);
    if (link == nullptr) return network_.removeLink(id);  // reports NotFound
    const FiberLink copy = *link;  // the Network is about to destroy the original
    auto result = network_.removeLink(id);
    if (!result.ok()) return result;
    unindexLink(copy);
    return {};
}

Result<void> Topology::addTransceiver(Transceiver transceiver) { return network_.addTransceiver(std::move(transceiver)); }
Result<void> Topology::addSwitchingElement(SwitchingElement element) {
    return network_.addSwitchingElement(std::move(element));
}
Result<void> Topology::attachTransceiver(NodeId node, TransceiverId transceiver) {
    return network_.attachTransceiver(node, transceiver);
}
Result<void> Topology::attachSwitchingElement(NodeId node, SwitchingElementId element) {
    return network_.attachSwitchingElement(node, element);
}
Result<void> Topology::removeTransceiver(TransceiverId id) { return network_.removeTransceiver(id); }
Result<void> Topology::removeSwitchingElement(SwitchingElementId id) { return network_.removeSwitchingElement(id); }

// ------------------------------------------------------------------- adjacency

std::span<const Adjacency> Topology::outgoing(NodeId node) const noexcept {
    const auto it = outgoing_.find(node);
    return it == outgoing_.end() ? std::span<const Adjacency>{} : std::span<const Adjacency>{it->second};
}

std::span<const Adjacency> Topology::incident(NodeId node) const noexcept {
    const auto it = incident_.find(node);
    return it == incident_.end() ? std::span<const Adjacency>{} : std::span<const Adjacency>{it->second};
}

std::vector<NodeId> Topology::neighbors(NodeId node) const {
    std::vector<NodeId> result;
    for (const Adjacency& edge : outgoing(node)) result.push_back(edge.neighbor);
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

std::vector<LinkId> Topology::linksBetween(NodeId from, NodeId to) const {
    std::vector<LinkId> result;
    for (const Adjacency& edge : outgoing(from)) {
        if (edge.neighbor == to) result.push_back(edge.link);
    }
    return result;
}

bool Topology::hasDirectLink(NodeId from, NodeId to) const {
    const auto edges = outgoing(from);
    return std::ranges::any_of(edges, [to](const Adjacency& e) { return e.neighbor == to; });
}

// ---------------------------------------------------------------- connectivity

std::vector<NodeId> Topology::reachSet(NodeId start, bool forward) const {
    std::vector<NodeId> visited;
    if (!hasNode(start)) return visited;
    std::unordered_set<NodeId> seen{start};
    std::deque<NodeId> queue{start};
    while (!queue.empty()) {
        const NodeId current = queue.front();
        queue.pop_front();
        visited.push_back(current);
        // Forward: follow traversable edges. Backward: follow edges that could lead *into* current.
        for (const Adjacency& edge : forward ? outgoing(current) : incident(current)) {
            if (!forward && !network_.findLink(edge.link)->allowsTraversal(edge.neighbor, current)) continue;
            if (seen.insert(edge.neighbor).second) queue.push_back(edge.neighbor);
        }
    }
    return visited;
}

bool Topology::isReachable(NodeId from, NodeId to) const {
    if (!hasNode(from) || !hasNode(to)) return false;
    const auto reached = reachSet(from, true);
    return std::ranges::find(reached, to) != reached.end();
}

std::vector<std::vector<NodeId>> Topology::connectedComponents() const {
    std::vector<std::vector<NodeId>> components;
    std::unordered_set<NodeId> assigned;
    for (const auto& entry : network_.nodes()) {
        if (assigned.contains(entry.first)) continue;
        std::vector<NodeId> component;
        std::deque<NodeId> queue{entry.first};
        assigned.insert(entry.first);
        while (!queue.empty()) {
            const NodeId current = queue.front();
            queue.pop_front();
            component.push_back(current);
            for (const Adjacency& edge : incident(current)) {
                if (assigned.insert(edge.neighbor).second) queue.push_back(edge.neighbor);
            }
        }
        std::ranges::sort(component);
        components.push_back(std::move(component));
    }
    return components;
}

bool Topology::isConnected() const { return connectedComponents().size() <= 1; }

bool Topology::isStronglyConnected() const {
    if (nodeCount() == 0) return true;
    const NodeId root = network_.nodes().begin()->first;
    return reachSet(root, true).size() == nodeCount() && reachSet(root, false).size() == nodeCount();
}

// ------------------------------------------------------------------ validation

ValidationReport Topology::validate() const {
    ValidationReport report;

    for (const auto& [id, node] : network_.nodes()) {
        const std::string label = "node " + std::to_string(id.value()) + " '" + node.name() + "'";
        if (node.switchingElement()) {
            const SwitchingElement* sw = network_.findSwitchingElement(*node.switchingElement());
            if (sw != nullptr && degree(id) > sw->portCount()) {
                report.issues.push_back({Severity::Error, IssueCode::PortCountExceeded,
                                         label + " has " + std::to_string(degree(id)) + " links but its switching element has only " +
                                             std::to_string(sw->portCount()) + " ports"});
            }
        }
        if (nodeCount() > 1 && degree(id) == 0) {
            report.issues.push_back({Severity::Warning, IssueCode::IsolatedNode, label + " has no links"});
        }
    }

    const auto components = connectedComponents();
    if (components.size() > 1) {
        report.issues.push_back({Severity::Warning, IssueCode::DisconnectedTopology,
                                 "topology has " + std::to_string(components.size()) + " disconnected components"});
    }
    return report;
}

std::string_view toString(IssueCode code) noexcept {
    switch (code) {
        case IssueCode::PortCountExceeded: return "PortCountExceeded";
        case IssueCode::IsolatedNode: return "IsolatedNode";
        case IssueCode::DisconnectedTopology: return "DisconnectedTopology";
        case IssueCode::AllocationExceedsCapacity: return "AllocationExceedsCapacity";
        case IssueCode::AllocationOnUnknownLink: return "AllocationOnUnknownLink";
        case IssueCode::ConnectionAllocationMismatch: return "ConnectionAllocationMismatch";
    }
    return "Unknown";
}

}  // namespace opticalnet

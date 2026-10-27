#include "opticalnet/validation/ReferenceModels.hpp"

#include <set>
#include <vector>

namespace opticalnet {

namespace {

double metricValue(const FiberLink& link, CostMetric metric) {
    switch (metric) {
        case CostMetric::HopCount: return 1.0;
        case CostMetric::Distance: return link.lengthKm();
        case CostMetric::Administrative: return link.administrativeCost();
    }
    return 0.0;
}

struct Search {
    const Topology& topology;
    NodeId destination;
    const RoutingConstraints& constraints;
    const FailureState* failures;
    std::vector<const FiberLink*> links;  // the whole link list, scanned at every step on purpose
    std::set<NodeId> visited;
    ReferenceRoute best;

    bool nodeAllowed(NodeId node) const {
        if (constraints.blockedNodes.contains(node)) return false;
        return !(failures && failures->isNodeFailed(node));
    }

    bool linkAllowed(const FiberLink& link) const {
        if (constraints.blockedLinks.contains(link.id())) return false;
        if (link.capacityChannels() < constraints.minLinkCapacityChannels) return false;
        if (constraints.linkFilter && !constraints.linkFilter(link)) return false;
        return !(failures && failures->isLinkFailed(link.id()));
    }

    void visit(NodeId at, double cost, std::size_t hops) {
        if (constraints.maxCost && cost > *constraints.maxCost + 1e-9) return;
        if (constraints.maxHops && hops > *constraints.maxHops) return;
        if (at == destination) {
            ++best.pathsConsidered;
            if (!best.found || cost < best.cost - 1e-9 || (cost < best.cost + 1e-9 && hops < best.hops)) {
                best.found = true;
                best.cost = cost;
                best.hops = hops;
            }
            return;
        }
        for (const FiberLink* link : links) {
            NodeId next{0};
            if (link->source() == at) next = link->target();
            else if (link->target() == at && link->direction() == LinkDirection::Bidirectional) next = link->source();
            else continue;
            if (!linkAllowed(*link) || !nodeAllowed(next) || visited.contains(next)) continue;
            visited.insert(next);
            visit(next, cost + metricValue(*link, constraints.metric), hops + 1);
            visited.erase(next);
        }
    }
};

}  // namespace

ReferenceRoute exhaustiveRoute(const Topology& topology, NodeId source, NodeId destination, const RoutingConstraints& constraints,
                               const FailureState* failures) {
    Search search{topology, destination, constraints, failures, {}, {}, {}};
    for (const auto& entry : topology.network().links()) search.links.push_back(&entry.second);
    if (!topology.hasNode(source) || !topology.hasNode(destination) || !search.nodeAllowed(source) || !search.nodeAllowed(destination))
        return {};
    search.visited.insert(source);
    search.visit(source, 0.0, 0);
    return search.best;
}

bool reachableByAnyPath(const Topology& topology, NodeId source, NodeId destination) {
    if (!topology.hasNode(source) || !topology.hasNode(destination)) return false;
    std::set<NodeId> seen{source};
    std::vector<NodeId> stack{source};
    while (!stack.empty()) {
        const NodeId at = stack.back();
        stack.pop_back();
        if (at == destination) return true;
        for (const auto& entry : topology.network().links()) {
            const FiberLink& link = entry.second;
            NodeId next{0};
            if (link.source() == at) next = link.target();
            else if (link.target() == at && link.direction() == LinkDirection::Bidirectional) next = link.source();
            else continue;
            if (seen.insert(next).second) stack.push_back(next);
        }
    }
    return false;
}

bool ShadowLedger::allocate(std::span<const LinkId> links, std::uint32_t demand) {
    if (demand == 0 || links.empty()) return false;
    std::map<LinkId, std::uint64_t> needed;
    for (const LinkId id : links) needed[id] += demand;
    for (const auto& [id, channels] : needed) {
        const FiberLink* link = topology_->findLink(id);
        if (!link) return false;
        if (allocated(id) + channels > link->capacityChannels()) return false;
    }
    for (const auto& [id, channels] : needed) counters_[id] += channels;
    return true;
}

bool ShadowLedger::release(std::span<const LinkId> links, std::uint32_t demand) {
    if (demand == 0 || links.empty()) return false;
    std::map<LinkId, std::uint64_t> needed;
    for (const LinkId id : links) needed[id] += demand;
    for (const auto& [id, channels] : needed)
        if (allocated(id) < channels) return false;
    for (const auto& [id, channels] : needed) {
        counters_[id] -= channels;
        if (counters_[id] == 0) counters_.erase(id);
    }
    return true;
}

std::uint64_t ShadowLedger::allocated(LinkId link) const {
    const auto it = counters_.find(link);
    return it == counters_.end() ? 0 : it->second;
}

std::uint64_t ShadowLedger::totalAllocated() const {
    std::uint64_t sum = 0;
    for (const auto& [id, channels] : counters_) sum += channels;
    return sum;
}

}  // namespace opticalnet

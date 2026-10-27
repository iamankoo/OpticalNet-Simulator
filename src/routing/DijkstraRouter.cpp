#include "opticalnet/routing/DijkstraRouter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace opticalnet {

namespace {

// A search state. Without a hop limit the layer is always 0, so a state is just a node.
// With a hop limit the layer is the number of links used so far, which makes the search
// exact for "cheapest path within N hops".
struct State {
    NodeId node;
    std::uint32_t layer;

    friend bool operator==(const State&, const State&) = default;
};

struct Step {  // how a label was reached
    State from;
    LinkId link;
};

struct Label {
    double cost;
    std::uint32_t hops;
    std::optional<Step> via;  // empty for the source
    bool settled = false;
    State state{NodeId(0), 0};  // the key, kept so the table can be rebuilt when it grows
};

// Labels of one search, stored contiguously and found through an open-addressing index (linear probing,
// power-of-two capacity, load factor <= 1/2). This replaces a node-based std::unordered_map, which allocated once
// per label and rehashed repeatedly while a search grew to thousands of states. Semantics are identical: one
// label per State, found by key. References returned by find()/emplace() are invalidated by the next emplace().
class LabelTable {
public:
    LabelTable() : slots_(64, kEmpty) {}

    Label* find(const State& key) {
        for (std::size_t i = hash(key) & mask();; i = (i + 1) & mask()) {
            const std::uint32_t slot = slots_[i];
            if (slot == kEmpty) return nullptr;
            if (labels_[slot].state == key) return &labels_[slot];
        }
    }

    Label& at(const State& key) { return *find(key); }

    Label& emplace(const State& key, Label label) {
        if ((labels_.size() + 1) * 2 > slots_.size()) grow();
        label.state = key;
        labels_.push_back(std::move(label));
        insertSlot(static_cast<std::uint32_t>(labels_.size() - 1));
        return labels_.back();
    }

private:
    static constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;

    static std::size_t hash(const State& s) noexcept {
        std::uint64_t x = (static_cast<std::uint64_t>(s.node.value()) << 32) | s.layer;
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        return static_cast<std::size_t>(x);
    }
    [[nodiscard]] std::size_t mask() const noexcept { return slots_.size() - 1; }

    void insertSlot(std::uint32_t index) {
        std::size_t i = hash(labels_[index].state) & mask();
        while (slots_[i] != kEmpty) i = (i + 1) & mask();
        slots_[i] = index;
    }
    void grow() {
        slots_.assign(slots_.size() * 2, kEmpty);
        for (std::uint32_t i = 0; i < labels_.size(); ++i) insertSlot(i);
    }

    std::vector<Label> labels_;
    std::vector<std::uint32_t> slots_;
};

// Strict total preference between two ways of reaching the same state (see DijkstraRouter.hpp).
bool improves(double cost, std::uint32_t hops, const Step& step, const Label& current) {
    if (cost != current.cost) return cost < current.cost;
    if (hops != current.hops) return hops < current.hops;
    if (!current.via) return false;
    if (step.from.node != current.via->from.node) return step.from.node < current.via->from.node;
    // Same predecessor: its adjacency list is scanned in ascending LinkId order, so the smallest
    // link already arrived first and a later (larger) one never replaces it.
    return false;
}

struct HeapEntry {
    double cost;
    std::uint32_t hops;
    State state;
};

// std::priority_queue is a max-heap, so "greater" yields the smallest (cost, hops, node, layer) on top.
struct HeapGreater {
    bool operator()(const HeapEntry& a, const HeapEntry& b) const noexcept {
        if (a.cost != b.cost) return a.cost > b.cost;
        if (a.hops != b.hops) return a.hops > b.hops;
        if (a.state.node != b.state.node) return a.state.node > b.state.node;
        return a.state.layer > b.state.layer;
    }
};

bool edgeAllowed(const RoutingConstraints& c, const Adjacency& edge, const FiberLink& link) {
    if (!c.blockedLinks.empty() && c.blockedLinks.contains(edge.link)) return false;
    if (!c.blockedNodes.empty() && c.blockedNodes.contains(edge.neighbor)) return false;
    if (link.capacityChannels() < c.minLinkCapacityChannels) return false;
    return !c.linkFilter || c.linkFilter(link);
}

// One Dijkstra run. `layered` enforces maxHops exactly; otherwise maxHops is ignored here
// and the caller checks the result. Returns nullopt if no path satisfies the constraints.
std::optional<Path> search(const Topology& topology, NodeId source, NodeId destination,
                           const RoutingConstraints& c, bool layered) {
    LabelTable labels;
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapGreater> heap;

    const State start{source, 0};
    labels.emplace(start, Label{0.0, 0, std::nullopt});
    heap.push({0.0, 0, start});

    while (!heap.empty()) {
        const HeapEntry top = heap.top();
        heap.pop();
        Label& label = labels.at(top.state);
        if (label.settled) continue;  // stale heap entry
        label.settled = true;
        const double cost = label.cost;
        const std::uint32_t hops = label.hops;
        // (`label` is not used after this point: the loop below may add labels, which invalidates references.)

        if (top.state.node == destination) {
            std::vector<NodeId> nodes;
            std::vector<LinkId> links;
            for (State s = top.state;;) {
                nodes.push_back(s.node);
                const auto& via = labels.at(s).via;
                if (!via) break;
                links.push_back(via->link);
                s = via->from;
            }
            std::ranges::reverse(nodes);
            std::ranges::reverse(links);
            return std::move(Path::create(std::move(nodes), std::move(links), cost, c.metric)).value();
        }

        if (layered && c.maxHops && hops >= *c.maxHops) continue;  // cannot extend further

        for (const Adjacency& edge : topology.outgoing(top.state.node)) {
            const FiberLink& link = edge.fiber != nullptr ? *edge.fiber : *topology.findLink(edge.link);
            if (!edgeAllowed(c, edge, link)) continue;

            const double newCost = cost + linkCost(link, c.metric);
            if (c.maxCost && newCost > *c.maxCost) continue;  // partial path already too expensive
            const std::uint32_t newHops = hops + 1;
            const State next{edge.neighbor, layered ? newHops : 0};
            const Step step{top.state, edge.link};

            Label* existing = labels.find(next);
            if (existing == nullptr) {
                labels.emplace(next, Label{newCost, newHops, step});
            } else if (!existing->settled && improves(newCost, newHops, step, *existing)) {
                existing->cost = newCost;
                existing->hops = newHops;
                existing->via = step;
            } else {
                continue;
            }
            heap.push({newCost, newHops, next});
        }
    }
    return std::nullopt;
}

}  // namespace

Result<Path> DijkstraRouter::findPath(const Topology& topology, NodeId source, NodeId destination,
                                      const RoutingConstraints& c) const {
    if (!topology.hasNode(source))
        return Error{ErrorCode::NotFound, "routing source node " + std::to_string(source.value()) + " does not exist"};
    if (!topology.hasNode(destination))
        return Error{ErrorCode::NotFound,
                     "routing destination node " + std::to_string(destination.value()) + " does not exist"};
    if (c.maxCost && !(std::isfinite(*c.maxCost) && *c.maxCost >= 0.0))
        return Error{ErrorCode::InvalidArgument, "routing maxCost must be a non-negative finite number"};

    const bool endpointBlocked = c.blockedNodes.contains(source) || c.blockedNodes.contains(destination);

    if (!endpointBlocked) {
        std::optional<Path> found = search(topology, source, destination, c, /*layered=*/false);
        // The plain search is the cheapest path overall; it is optimal only if it also fits the hop limit.
        if (found && c.maxHops && found->hopCount() > *c.maxHops) {
            found = search(topology, source, destination, c, /*layered=*/true);
        }
        if (found) return std::move(*found);
    }

    // No feasible path. Say whether the topology itself offers no route, or only the constraints do.
    if (c.isUnconstrained()) return Error{ErrorCode::NoRoute, "no route from node " + std::to_string(source.value()) +
                                                                  " to node " + std::to_string(destination.value())};
    RoutingConstraints plain;
    plain.metric = c.metric;
    if (search(topology, source, destination, plain, /*layered=*/false)) {
        return Error{ErrorCode::NoFeasibleRoute,
                     "a route from node " + std::to_string(source.value()) + " to node " +
                         std::to_string(destination.value()) + " exists but none satisfies the constraints"};
    }
    return Error{ErrorCode::NoRoute, "no route from node " + std::to_string(source.value()) + " to node " +
                                         std::to_string(destination.value())};
}

}  // namespace opticalnet

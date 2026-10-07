#pragma once

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "opticalnet/core/Network.hpp"
#include "opticalnet/topology/LinkCost.hpp"
#include "opticalnet/topology/ValidationReport.hpp"

namespace opticalnet {

// One entry of the adjacency list: "from this node, `link` leads to `neighbor`".
struct Adjacency {
    NodeId neighbor;
    LinkId link;

    friend constexpr bool operator==(const Adjacency&, const Adjacency&) = default;
};

// Graph view of a Network.
//
//   Node -> vertex, FiberLink -> edge, link cost -> edge weight.
//
// Topology owns the one and only Network (no second copy of the domain objects) and
// keeps adjacency lists in sync with it. All mutation goes through Topology so the
// index can never disagree with the Network; the Network itself is exposed read-only.
//
// Link semantics (see FiberLink):
//  - Bidirectional links (default) appear in the outgoing list of both endpoints.
//  - Directed links appear in the outgoing list of their source only.
//  - Parallel links between the same nodes are allowed (distinct LinkIds); self-links are not.
//
// Adjacency lists are kept sorted by LinkId, so iteration order is deterministic.
// Spans returned by outgoing()/incident() are invalidated by any mutation.
class Topology {
public:
    Topology() = default;
    explicit Topology(Network network);  // builds the adjacency index; a Network is always consistent

    // --- mutation (same rules as Network) ---
    Result<void> addNode(Node node);
    Result<void> removeNode(NodeId id);  // fails while links touch the node
    Result<void> addLink(FiberLink link);
    Result<void> removeLink(LinkId id);
    Result<void> addTransceiver(Transceiver transceiver);
    Result<void> addSwitchingElement(SwitchingElement element);
    Result<void> attachTransceiver(NodeId node, TransceiverId transceiver);
    Result<void> attachSwitchingElement(NodeId node, SwitchingElementId element);
    Result<void> removeTransceiver(TransceiverId id);
    Result<void> removeSwitchingElement(SwitchingElementId id);

    // --- lookup ---
    [[nodiscard]] const Network& network() const noexcept { return network_; }
    [[nodiscard]] bool hasNode(NodeId id) const noexcept { return network_.findNode(id) != nullptr; }
    [[nodiscard]] bool hasLink(LinkId id) const noexcept { return network_.findLink(id) != nullptr; }
    [[nodiscard]] const Node* findNode(NodeId id) const noexcept { return network_.findNode(id); }
    [[nodiscard]] const FiberLink* findLink(LinkId id) const noexcept { return network_.findLink(id); }
    [[nodiscard]] std::size_t nodeCount() const noexcept { return network_.nodeCount(); }
    [[nodiscard]] std::size_t linkCount() const noexcept { return network_.linkCount(); }

    // --- adjacency (what routing consumes) ---
    // Edges traversable *from* `node`. Empty for unknown nodes.
    [[nodiscard]] std::span<const Adjacency> outgoing(NodeId node) const noexcept;
    // Every link touching `node` regardless of direction; `neighbor` is the other endpoint.
    [[nodiscard]] std::span<const Adjacency> incident(NodeId node) const noexcept;
    // Distinct nodes reachable over one traversable link, ascending by id.
    [[nodiscard]] std::vector<NodeId> neighbors(NodeId node) const;
    // Links usable for travelling from `from` to `to`, ascending by id (several if parallel).
    [[nodiscard]] std::vector<LinkId> linksBetween(NodeId from, NodeId to) const;
    [[nodiscard]] bool hasDirectLink(NodeId from, NodeId to) const;
    // Number of links touching the node (each consumes one port on that node).
    [[nodiscard]] std::size_t degree(NodeId node) const noexcept { return incident(node).size(); }

    // --- connectivity ---
    // True if `to` can be reached from `from` honouring link direction. A node reaches itself.
    [[nodiscard]] bool isReachable(NodeId from, NodeId to) const;
    // Weakly connected components (direction ignored), each sorted by id, ordered by smallest id.
    [[nodiscard]] std::vector<std::vector<NodeId>> connectedComponents() const;
    // Physical connectivity: one weak component. An empty topology counts as connected.
    [[nodiscard]] bool isConnected() const;
    // Every node can reach every other node honouring direction. Empty counts as true.
    [[nodiscard]] bool isStronglyConnected() const;

    // --- validation ---
    // "Valid" (no errors) is distinct from "connected"; see ValidationReport.
    [[nodiscard]] ValidationReport validate() const;

private:
    using AdjacencyList = std::vector<Adjacency>;
    using Index = std::unordered_map<NodeId, AdjacencyList>;

    void indexLink(const FiberLink& link);
    void unindexLink(const FiberLink& link);
    [[nodiscard]] std::vector<NodeId> reachSet(NodeId start, bool forward) const;

    Network network_;
    Index outgoing_;
    Index incident_;
};

}  // namespace opticalnet

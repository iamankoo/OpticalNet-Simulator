#pragma once

#include "opticalnet/routing/IRoutingAlgorithm.hpp"

namespace opticalnet {

// Dijkstra shortest path over Topology::outgoing() with a binary min-heap:
// O((V + E) log V) on the adjacency lists, no scanning of the whole network per node.
//
// Semantics
//  - Direction: only edges in outgoing(node) are followed, so a directed link is used
//    source -> target only and a bidirectional link both ways.
//  - Parallel links are separate edges; the cheapest (under the metric) wins.
//  - All edge costs are positive (guaranteed by FiberLink validation), as Dijkstra needs.
//  - source == destination yields the trivial path: [source], no links, cost 0, 0 hops
//    (provided the node is not blocked).
//
// Constraints
//  - maxCost, minLinkCapacityChannels, blocked nodes/links and linkFilter prune edges
//    during exploration (partial paths over maxCost are never extended).
//  - maxHops is exact. A plain Dijkstra run ignores it; if its (cheapest) result has too
//    many hops, the search is repeated over (node, hops-used) states, which finds the
//    cheapest path within the hop limit. That costs O(maxHops * (V + E) log V) in the
//    worst case but only happens when the limit actually binds.
//
// Deterministic tie-breaking - among routes to the same node, the preferred one has, in order:
//    1. lower cost                      2. fewer hops
//    3. smaller predecessor NodeId      4. smaller LinkId of the last link
//  (4 follows from adjacency lists being scanned in ascending LinkId order.) Heap pops follow
//  (cost, hops, NodeId), so the result never depends on the order links were added. Costs are compared as doubles,
//  exactly: equal-cost ties are only guaranteed for costs that sum exactly (e.g. integers).
class DijkstraRouter final : public IRoutingAlgorithm {
public:
    [[nodiscard]] Result<Path> findPath(const Topology& topology, NodeId source, NodeId destination,
                                        const RoutingConstraints& constraints) const override;
};

}  // namespace opticalnet

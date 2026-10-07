#pragma once

#include <cstddef>
#include <vector>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/topology/LinkCost.hpp"

namespace opticalnet {

// An ordered route through the topology, source to destination:
//
//   nodes: [A, B, C, D]      links: [A-B, B-C, C-D]      cost: 25      hops: 3
//
// The traversed *links* are kept, not just the nodes, because with parallel links the
// node sequence alone does not say which fiber is used, and resource allocation
// (Phase 4) needs exactly that.
//
// Immutable value type. Invariant: nodes.size() == links.size() + 1 (a trivial path
// from a node to itself has one node, no links, cost 0).
class Path {
public:
    [[nodiscard]] static Result<Path> create(std::vector<NodeId> nodes, std::vector<LinkId> links,
                                             double cost, CostMetric metric);

    [[nodiscard]] NodeId source() const noexcept { return nodes_.front(); }
    [[nodiscard]] NodeId destination() const noexcept { return nodes_.back(); }
    [[nodiscard]] const std::vector<NodeId>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<LinkId>& links() const noexcept { return links_; }
    [[nodiscard]] double cost() const noexcept { return cost_; }
    // The metric `cost()` is expressed in.
    [[nodiscard]] CostMetric metric() const noexcept { return metric_; }
    [[nodiscard]] std::size_t hopCount() const noexcept { return links_.size(); }
    [[nodiscard]] bool isTrivial() const noexcept { return links_.empty(); }

    friend bool operator==(const Path&, const Path&) = default;

private:
    Path(std::vector<NodeId> nodes, std::vector<LinkId> links, double cost, CostMetric metric)
        : nodes_(std::move(nodes)), links_(std::move(links)), cost_(cost), metric_(metric) {}

    std::vector<NodeId> nodes_;
    std::vector<LinkId> links_;
    double cost_;
    CostMetric metric_;
};

}  // namespace opticalnet

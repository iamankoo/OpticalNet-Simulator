#include "opticalnet/routing/Path.hpp"

#include <cmath>

namespace opticalnet {

Result<Path> Path::create(std::vector<NodeId> nodes, std::vector<LinkId> links, double cost, CostMetric metric) {
    if (nodes.empty()) return Error{ErrorCode::InvalidArgument, "path needs at least one node"};
    if (nodes.size() != links.size() + 1)
        return Error{ErrorCode::InvalidArgument, "path needs exactly one more node than links"};
    if (!std::isfinite(cost) || cost < 0.0)
        return Error{ErrorCode::InvalidArgument, "path cost must be a non-negative finite number"};
    if (links.empty() && cost != 0.0) return Error{ErrorCode::InvalidArgument, "a path without links must cost 0"};
    return Path(std::move(nodes), std::move(links), cost, metric);
}

}  // namespace opticalnet

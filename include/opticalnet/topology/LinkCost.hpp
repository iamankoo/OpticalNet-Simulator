#pragma once

#include <string_view>

#include "opticalnet/core/FiberLink.hpp"

namespace opticalnet {

// Which link property serves as the edge weight. Phase 3 routing picks a metric and
// reads weights through linkCost(); topology itself never runs a path search.
enum class CostMetric {
    HopCount,        // every link costs 1
    Distance,        // physical length in km
    Administrative,  // operator-assigned weight (FiberLink::administrativeCost)
};

[[nodiscard]] std::string_view toString(CostMetric metric) noexcept;

// Edge weight of `link` under `metric`. Always positive and finite for a valid FiberLink.
[[nodiscard]] double linkCost(const FiberLink& link, CostMetric metric) noexcept;

}  // namespace opticalnet

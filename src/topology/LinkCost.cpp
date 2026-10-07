#include "opticalnet/topology/LinkCost.hpp"

namespace opticalnet {

std::string_view toString(CostMetric metric) noexcept {
    switch (metric) {
        case CostMetric::HopCount: return "hop_count";
        case CostMetric::Distance: return "distance";
        case CostMetric::Administrative: return "administrative";
    }
    return "unknown";
}

double linkCost(const FiberLink& link, CostMetric metric) noexcept {
    switch (metric) {
        case CostMetric::HopCount: return 1.0;
        case CostMetric::Distance: return link.lengthKm();
        case CostMetric::Administrative: return link.administrativeCost();
    }
    return 1.0;
}

}  // namespace opticalnet

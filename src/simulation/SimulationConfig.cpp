#include "opticalnet/simulation/SimulationConfig.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace opticalnet {

namespace {

bool nonNegativeFinite(double v) { return std::isfinite(v) && v >= 0.0; }

}  // namespace

Result<void> SimulationConfig::validate() const {
    const auto bad = [](std::string message) { return Result<void>{Error{ErrorCode::InvalidArgument, std::move(message)}}; };
    if (!nonNegativeFinite(endTime)) return bad("simulation endTime must be a non-negative finite number");
    if (!nonNegativeFinite(arrivalRate)) return bad("arrivalRate must be a non-negative finite number");
    if (!(std::isfinite(meanLifetime) && meanLifetime > 0.0)) return bad("meanLifetime must be a positive finite number");
    if (minCapacityChannels == 0) return bad("minCapacityChannels must be at least 1");
    if (minCapacityChannels > maxCapacityChannels) return bad("minCapacityChannels must not exceed maxCapacityChannels");
    if (maxRequests && *maxRequests > std::numeric_limits<std::uint32_t>::max())
        return bad("maxRequests must fit in 32 bits (connection ids are 32-bit)");
    return {};
}

}  // namespace opticalnet

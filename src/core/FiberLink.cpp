#include "opticalnet/core/FiberLink.hpp"

#include "Validation.hpp"

namespace opticalnet {

std::string_view toString(LinkDirection direction) noexcept {
    switch (direction) {
        case LinkDirection::Bidirectional: return "bidirectional";
        case LinkDirection::Directed: return "directed";
    }
    return "unknown";
}

Result<FiberLink> FiberLink::create(LinkId id, std::string name, NodeId source, NodeId target,
                                    double lengthKm, double attenuationDbPerKm,
                                    std::uint32_t capacityChannels, LinkDirection direction,
                                    double administrativeCost) {
    if (detail::isBlank(name)) return detail::invalid("link name must not be blank");
    if (source == target) return detail::invalid("link endpoints must be different nodes");
    if (!detail::isPositiveFinite(lengthKm)) return detail::invalid("link length must be a positive finite number");
    if (!detail::isNonNegativeFinite(attenuationDbPerKm))
        return detail::invalid("link attenuation must be a non-negative finite number");
    if (capacityChannels == 0) return detail::invalid("link capacity must be at least one channel");
    if (!detail::isPositiveFinite(administrativeCost))
        return detail::invalid("link administrative cost must be a positive finite number");
    return FiberLink(id, std::move(name), source, target, lengthKm, attenuationDbPerKm, capacityChannels,
                     direction, administrativeCost);
}

bool FiberLink::allowsTraversal(NodeId from, NodeId to) const noexcept {
    if (from == source_ && to == target_) return true;
    return direction_ == LinkDirection::Bidirectional && from == target_ && to == source_;
}

std::string FiberLink::describe() const {
    return "Link " + std::to_string(id_.value()) + " '" + name_ + "' (" + std::to_string(source_.value()) +
           (direction_ == LinkDirection::Directed ? " -> " : " <-> ") + std::to_string(target_.value()) + ", " + std::to_string(lengthKm_) + " km, " +
           std::to_string(capacityChannels_) + " channels)";
}

}  // namespace opticalnet

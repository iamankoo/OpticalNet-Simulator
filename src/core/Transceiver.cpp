#include "opticalnet/core/Transceiver.hpp"

#include "Validation.hpp"

namespace opticalnet {

Result<Transceiver> Transceiver::create(TransceiverId id, std::string name, double dataRateGbps,
                                        double reachKm) {
    if (detail::isBlank(name)) return detail::invalid("transceiver name must not be blank");
    if (!detail::isPositiveFinite(dataRateGbps))
        return detail::invalid("transceiver data rate must be a positive finite number");
    if (!detail::isPositiveFinite(reachKm))
        return detail::invalid("transceiver reach must be a positive finite number");
    return Transceiver(id, std::move(name), dataRateGbps, reachKm);
}

bool Transceiver::canReach(double distanceKm) const noexcept {
    return distanceKm >= 0.0 && distanceKm <= reachKm_;
}

bool Transceiver::supportsRate(double demandGbps) const noexcept {
    return demandGbps >= 0.0 && demandGbps <= dataRateGbps_;
}

std::string Transceiver::describe() const {
    return "Transceiver " + std::to_string(id_.value()) + " '" + name_ + "' (" +
           std::to_string(dataRateGbps_) + " Gbps, reach " + std::to_string(reachKm_) + " km)";
}

}  // namespace opticalnet

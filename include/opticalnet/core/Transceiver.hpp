#pragma once

#include <string>
#include <utility>

#include "opticalnet/core/Element.hpp"
#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"

namespace opticalnet {

// Optical transmitter/receiver pair: a fixed data rate and a maximum
// unregenerated optical reach.
class Transceiver final : public INetworkElement {
public:
    [[nodiscard]] static Result<Transceiver> create(TransceiverId id, std::string name,
                                                    double dataRateGbps, double reachKm);

    [[nodiscard]] TransceiverId id() const noexcept { return id_; }
    [[nodiscard]] double dataRateGbps() const noexcept { return dataRateGbps_; }
    [[nodiscard]] double reachKm() const noexcept { return reachKm_; }

    // True if a signal can travel `distanceKm` (inclusive) without regeneration.
    [[nodiscard]] bool canReach(double distanceKm) const noexcept;
    // True if this transceiver's rate can carry `demandGbps`.
    [[nodiscard]] bool supportsRate(double demandGbps) const noexcept;

    [[nodiscard]] ElementKind kind() const noexcept override { return ElementKind::Transceiver; }
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] std::string describe() const override;

private:
    Transceiver(TransceiverId id, std::string name, double rate, double reach)
        : id_(id), name_(std::move(name)), dataRateGbps_(rate), reachKm_(reach) {}

    TransceiverId id_;
    std::string name_;
    double dataRateGbps_;
    double reachKm_;
};

}  // namespace opticalnet

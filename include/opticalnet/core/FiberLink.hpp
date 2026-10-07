#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "opticalnet/core/Element.hpp"
#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"

namespace opticalnet {

// Optical fiber between two distinct nodes. Holds static properties only:
// how much capacity exists, not how much is in use. Whether the link is treated
// as directed or bidirectional is decided by the topology layer (Phase 2).
class FiberLink final : public INetworkElement {
public:
    [[nodiscard]] static Result<FiberLink> create(LinkId id, std::string name, NodeId source,
                                                  NodeId target, double lengthKm,
                                                  double attenuationDbPerKm,
                                                  std::uint32_t capacityChannels);

    [[nodiscard]] LinkId id() const noexcept { return id_; }
    [[nodiscard]] NodeId source() const noexcept { return source_; }
    [[nodiscard]] NodeId target() const noexcept { return target_; }
    [[nodiscard]] double lengthKm() const noexcept { return lengthKm_; }
    [[nodiscard]] double attenuationDbPerKm() const noexcept { return attenuationDbPerKm_; }
    [[nodiscard]] std::uint32_t capacityChannels() const noexcept { return capacityChannels_; }

    [[nodiscard]] double totalLossDb() const noexcept { return lengthKm_ * attenuationDbPerKm_; }
    [[nodiscard]] bool touches(NodeId node) const noexcept { return node == source_ || node == target_; }

    [[nodiscard]] ElementKind kind() const noexcept override { return ElementKind::FiberLink; }
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] std::string describe() const override;

private:
    FiberLink(LinkId id, std::string name, NodeId source, NodeId target, double length,
              double attenuation, std::uint32_t capacity)
        : id_(id), name_(std::move(name)), source_(source), target_(target),
          lengthKm_(length), attenuationDbPerKm_(attenuation), capacityChannels_(capacity) {}

    LinkId id_;
    std::string name_;
    NodeId source_;
    NodeId target_;
    double lengthKm_;
    double attenuationDbPerKm_;
    std::uint32_t capacityChannels_;
};

}  // namespace opticalnet

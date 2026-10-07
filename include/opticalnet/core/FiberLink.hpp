#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "opticalnet/core/Element.hpp"
#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"

namespace opticalnet {

enum class LinkDirection {
    Bidirectional,  // traffic may flow source->target and target->source (default)
    Directed,       // traffic may flow source->target only
};

[[nodiscard]] std::string_view toString(LinkDirection direction) noexcept;

// Optical fiber between two distinct nodes. Holds static properties only:
// how much capacity exists, not how much is in use.
//
// Direction: a link is bidirectional by default, modelling a physical fiber pair
// carrying one direction each; cost applies to each traversal. A Directed link models a
// one-way strand (source -> target only).
//
// Capacity: `capacityChannels` is a single pool per link, shared by both directions of a
// bidirectional link (a deliberately conservative model; see ResourceManager).
//
// Cost: `administrativeCost` is a positive, unitless weight chosen by the operator
// (default 1). Other routing metrics (hops, distance) derive from the link itself.
class FiberLink final : public INetworkElement {
public:
    [[nodiscard]] static Result<FiberLink> create(LinkId id, std::string name, NodeId source,
                                                  NodeId target, double lengthKm,
                                                  double attenuationDbPerKm,
                                                  std::uint32_t capacityChannels,
                                                  LinkDirection direction = LinkDirection::Bidirectional,
                                                  double administrativeCost = 1.0);

    [[nodiscard]] LinkId id() const noexcept { return id_; }
    [[nodiscard]] NodeId source() const noexcept { return source_; }
    [[nodiscard]] NodeId target() const noexcept { return target_; }
    [[nodiscard]] double lengthKm() const noexcept { return lengthKm_; }
    [[nodiscard]] double attenuationDbPerKm() const noexcept { return attenuationDbPerKm_; }
    [[nodiscard]] std::uint32_t capacityChannels() const noexcept { return capacityChannels_; }

    [[nodiscard]] LinkDirection direction() const noexcept { return direction_; }
    [[nodiscard]] double administrativeCost() const noexcept { return administrativeCost_; }

    // True if traffic may travel from `from` to `to` over this link.
    [[nodiscard]] bool allowsTraversal(NodeId from, NodeId to) const noexcept;

    [[nodiscard]] double totalLossDb() const noexcept { return lengthKm_ * attenuationDbPerKm_; }
    [[nodiscard]] bool touches(NodeId node) const noexcept { return node == source_ || node == target_; }

    [[nodiscard]] ElementKind kind() const noexcept override { return ElementKind::FiberLink; }
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] std::string describe() const override;

private:
    FiberLink(LinkId id, std::string name, NodeId source, NodeId target, double length,
              double attenuation, std::uint32_t capacity, LinkDirection direction, double cost)
        : id_(id), name_(std::move(name)), source_(source), target_(target),
          lengthKm_(length), attenuationDbPerKm_(attenuation), capacityChannels_(capacity),
          direction_(direction), administrativeCost_(cost) {}

    LinkId id_;
    std::string name_;
    NodeId source_;
    NodeId target_;
    double lengthKm_;
    double attenuationDbPerKm_;
    std::uint32_t capacityChannels_;
    LinkDirection direction_;
    double administrativeCost_;
};

}  // namespace opticalnet

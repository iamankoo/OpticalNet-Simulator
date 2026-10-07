#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "opticalnet/core/Element.hpp"
#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"

namespace opticalnet {

enum class SwitchingType {
    Roadm,  // reconfigurable optical add/drop multiplexer
    Oxc,    // optical cross-connect
};

[[nodiscard]] std::string_view toString(SwitchingType type) noexcept;

// Optical switch with numbered ports 0..portCount-1. Phase 1 models only the
// static switching constraints; cross-connect state belongs to resource management.
class SwitchingElement final : public INetworkElement {
public:
    [[nodiscard]] static Result<SwitchingElement> create(SwitchingElementId id, std::string name,
                                                         SwitchingType type, std::uint32_t portCount);

    [[nodiscard]] SwitchingElementId id() const noexcept { return id_; }
    [[nodiscard]] SwitchingType type() const noexcept { return type_; }
    [[nodiscard]] std::uint32_t portCount() const noexcept { return portCount_; }

    [[nodiscard]] bool hasPort(std::uint32_t port) const noexcept { return port < portCount_; }
    // A cross-connect is allowed between two distinct, existing ports.
    [[nodiscard]] bool canSwitch(std::uint32_t inPort, std::uint32_t outPort) const noexcept;

    [[nodiscard]] ElementKind kind() const noexcept override { return ElementKind::SwitchingElement; }
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] std::string describe() const override;

private:
    SwitchingElement(SwitchingElementId id, std::string name, SwitchingType type, std::uint32_t ports)
        : id_(id), name_(std::move(name)), type_(type), portCount_(ports) {}

    SwitchingElementId id_;
    std::string name_;
    SwitchingType type_;
    std::uint32_t portCount_;
};

}  // namespace opticalnet

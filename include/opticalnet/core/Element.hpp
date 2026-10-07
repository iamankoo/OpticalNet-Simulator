#pragma once

#include <string>

namespace opticalnet {

enum class ElementKind { Node, FiberLink, Transceiver, SwitchingElement };

// Common abstraction for everything that lives in a Network.
// Concrete elements are value types; the Network owns them.
class INetworkElement {
public:
    virtual ~INetworkElement() = default;

    [[nodiscard]] virtual ElementKind kind() const noexcept = 0;
    [[nodiscard]] virtual const std::string& name() const noexcept = 0;
    [[nodiscard]] virtual std::string describe() const = 0;

protected:
    INetworkElement() = default;
    INetworkElement(const INetworkElement&) = default;
    INetworkElement(INetworkElement&&) = default;
    INetworkElement& operator=(const INetworkElement&) = default;
    INetworkElement& operator=(INetworkElement&&) = default;
};

}  // namespace opticalnet

#include "opticalnet/core/SwitchingElement.hpp"

#include "Validation.hpp"

namespace opticalnet {

std::string_view toString(SwitchingType type) noexcept {
    switch (type) {
        case SwitchingType::Roadm: return "ROADM";
        case SwitchingType::Oxc: return "OXC";
    }
    return "Unknown";
}

Result<SwitchingElement> SwitchingElement::create(SwitchingElementId id, std::string name,
                                                  SwitchingType type, std::uint32_t portCount) {
    if (detail::isBlank(name)) return detail::invalid("switching element name must not be blank");
    if (portCount == 0) return detail::invalid("switching element needs at least one port");
    return SwitchingElement(id, std::move(name), type, portCount);
}

bool SwitchingElement::canSwitch(std::uint32_t inPort, std::uint32_t outPort) const noexcept {
    return hasPort(inPort) && hasPort(outPort) && inPort != outPort;
}

std::string SwitchingElement::describe() const {
    return std::string(toString(type_)) + " " + std::to_string(id_.value()) + " '" + name_ + "' (" +
           std::to_string(portCount_) + " ports)";
}

}  // namespace opticalnet

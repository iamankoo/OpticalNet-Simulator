#include "opticalnet/core/Node.hpp"

#include "Validation.hpp"

namespace opticalnet {

Result<Node> Node::create(NodeId id, std::string name) {
    if (detail::isBlank(name)) return detail::invalid("node name must not be blank");
    return Node(id, std::move(name));
}

std::string Node::describe() const {
    return "Node " + std::to_string(id_.value()) + " '" + name_ + "' (" +
           (switchingElement_ ? "switch " + std::to_string(switchingElement_->value()) : "no switch") +
           ", " + std::to_string(transceivers_.size()) + " transceivers)";
}

}  // namespace opticalnet

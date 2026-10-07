#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "opticalnet/core/Element.hpp"
#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"

namespace opticalnet {

class Network;

// A network site. A node refers to its switching element and transceivers by id;
// the Network owns those objects and validates the references.
class Node final : public INetworkElement {
public:
    [[nodiscard]] static Result<Node> create(NodeId id, std::string name);

    [[nodiscard]] NodeId id() const noexcept { return id_; }
    [[nodiscard]] const std::optional<SwitchingElementId>& switchingElement() const noexcept {
        return switchingElement_;
    }
    [[nodiscard]] const std::vector<TransceiverId>& transceivers() const noexcept { return transceivers_; }

    [[nodiscard]] ElementKind kind() const noexcept override { return ElementKind::Node; }
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] std::string describe() const override;

private:
    friend class Network;  // the only place that may modify attachments

    Node(NodeId id, std::string name) : id_(id), name_(std::move(name)) {}

    NodeId id_;
    std::string name_;
    std::optional<SwitchingElementId> switchingElement_;
    std::vector<TransceiverId> transceivers_;
};

}  // namespace opticalnet

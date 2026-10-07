#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/FiberLink.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/core/Node.hpp"
#include "opticalnet/core/SwitchingElement.hpp"
#include "opticalnet/core/Transceiver.hpp"

namespace opticalnet {

// Owns every element and enforces the relationships between them:
//  - ids are unique per element type
//  - a link may only join nodes that exist
//  - a node may only reference a switching element / transceivers that exist
//  - a switching element or transceiver belongs to at most one node
//  - nothing that is still referenced can be removed
// Containers are ordered by id so iteration is deterministic.
// Registration order: transceivers/switching elements, then nodes, then links.
class Network {
public:
    // --- registration ---
    Result<void> addTransceiver(Transceiver transceiver);
    Result<void> addSwitchingElement(SwitchingElement element);
    Result<void> addNode(Node node);
    Result<void> addLink(FiberLink link);

    // --- attachment (node <-> equipment) ---
    Result<void> attachSwitchingElement(NodeId node, SwitchingElementId element);
    Result<void> attachTransceiver(NodeId node, TransceiverId transceiver);

    // --- removal ---
    Result<void> removeLink(LinkId id);
    Result<void> removeNode(NodeId id);                          // fails while links touch it
    Result<void> removeSwitchingElement(SwitchingElementId id);  // fails while attached
    Result<void> removeTransceiver(TransceiverId id);            // fails while attached

    // --- lookup (nullptr when absent) ---
    [[nodiscard]] const Node* findNode(NodeId id) const noexcept;
    [[nodiscard]] const FiberLink* findLink(LinkId id) const noexcept;
    [[nodiscard]] const Transceiver* findTransceiver(TransceiverId id) const noexcept;
    [[nodiscard]] const SwitchingElement* findSwitchingElement(SwitchingElementId id) const noexcept;

    // --- queries ---
    [[nodiscard]] std::vector<LinkId> linksOf(NodeId node) const;  // links touching the node
    [[nodiscard]] const std::map<NodeId, Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::map<LinkId, FiberLink>& links() const noexcept { return links_; }
    [[nodiscard]] const std::map<TransceiverId, Transceiver>& transceivers() const noexcept {
        return transceivers_;
    }
    [[nodiscard]] const std::map<SwitchingElementId, SwitchingElement>& switchingElements() const noexcept {
        return switchingElements_;
    }

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t linkCount() const noexcept { return links_.size(); }

private:
    [[nodiscard]] bool switchingElementInUse(SwitchingElementId id) const noexcept;
    [[nodiscard]] bool transceiverInUse(TransceiverId id) const noexcept;

    std::map<NodeId, Node> nodes_;
    std::map<LinkId, FiberLink> links_;
    std::map<TransceiverId, Transceiver> transceivers_;
    std::map<SwitchingElementId, SwitchingElement> switchingElements_;
};

}  // namespace opticalnet

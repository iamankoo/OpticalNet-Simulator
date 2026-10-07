#include "opticalnet/core/Network.hpp"

#include <algorithm>

namespace opticalnet {

namespace {

template <class Id>
std::string label(const char* what, Id id) {
    return std::string(what) + " " + std::to_string(id.value());
}

Error err(ErrorCode code, std::string message) { return Error{code, std::move(message)}; }

template <class Map, class Id>
const typename Map::mapped_type* findIn(const Map& map, Id id) noexcept {
    const auto it = map.find(id);
    return it == map.end() ? nullptr : &it->second;
}

}  // namespace

// ---------------------------------------------------------------- registration

Result<void> Network::addTransceiver(Transceiver transceiver) {
    const auto id = transceiver.id();
    if (transceivers_.contains(id)) return err(ErrorCode::DuplicateId, label("transceiver", id) + " already exists");
    transceivers_.emplace(id, std::move(transceiver));
    return {};
}

Result<void> Network::addSwitchingElement(SwitchingElement element) {
    const auto id = element.id();
    if (switchingElements_.contains(id))
        return err(ErrorCode::DuplicateId, label("switching element", id) + " already exists");
    switchingElements_.emplace(id, std::move(element));
    return {};
}

Result<void> Network::addNode(Node node) {
    const auto id = node.id();
    if (nodes_.contains(id)) return err(ErrorCode::DuplicateId, label("node", id) + " already exists");
    nodes_.emplace(id, std::move(node));
    return {};
}

Result<void> Network::addLink(FiberLink link) {
    const auto id = link.id();
    if (links_.contains(id)) return err(ErrorCode::DuplicateId, label("link", id) + " already exists");
    if (!nodes_.contains(link.source()))
        return err(ErrorCode::NotFound, label("link", id) + " source " + label("node", link.source()) + " does not exist");
    if (!nodes_.contains(link.target()))
        return err(ErrorCode::NotFound, label("link", id) + " target " + label("node", link.target()) + " does not exist");
    links_.emplace(id, std::move(link));
    return {};
}

// ------------------------------------------------------------------ attachment

Result<void> Network::attachSwitchingElement(NodeId nodeId, SwitchingElementId elementId) {
    const auto node = nodes_.find(nodeId);
    if (node == nodes_.end()) return err(ErrorCode::NotFound, label("node", nodeId) + " does not exist");
    if (!switchingElements_.contains(elementId))
        return err(ErrorCode::NotFound, label("switching element", elementId) + " does not exist");
    if (node->second.switchingElement())
        return err(ErrorCode::ConstraintViolation, label("node", nodeId) + " already has a switching element");
    if (switchingElementInUse(elementId))
        return err(ErrorCode::ConstraintViolation, label("switching element", elementId) + " is attached to another node");
    node->second.switchingElement_ = elementId;
    return {};
}

Result<void> Network::attachTransceiver(NodeId nodeId, TransceiverId transceiverId) {
    const auto node = nodes_.find(nodeId);
    if (node == nodes_.end()) return err(ErrorCode::NotFound, label("node", nodeId) + " does not exist");
    if (!transceivers_.contains(transceiverId))
        return err(ErrorCode::NotFound, label("transceiver", transceiverId) + " does not exist");
    if (transceiverInUse(transceiverId))
        return err(ErrorCode::ConstraintViolation, label("transceiver", transceiverId) + " is already attached to a node");
    node->second.transceivers_.push_back(transceiverId);
    return {};
}

// --------------------------------------------------------------------- removal

Result<void> Network::removeLink(LinkId id) {
    if (links_.erase(id) == 0) return err(ErrorCode::NotFound, label("link", id) + " does not exist");
    return {};
}

Result<void> Network::removeNode(NodeId id) {
    if (!nodes_.contains(id)) return err(ErrorCode::NotFound, label("node", id) + " does not exist");
    if (!linksOf(id).empty())
        return err(ErrorCode::ConstraintViolation, label("node", id) + " still has links attached");
    nodes_.erase(id);
    return {};
}

Result<void> Network::removeSwitchingElement(SwitchingElementId id) {
    if (!switchingElements_.contains(id)) return err(ErrorCode::NotFound, label("switching element", id) + " does not exist");
    if (switchingElementInUse(id))
        return err(ErrorCode::ConstraintViolation, label("switching element", id) + " is attached to a node");
    switchingElements_.erase(id);
    return {};
}

Result<void> Network::removeTransceiver(TransceiverId id) {
    if (!transceivers_.contains(id)) return err(ErrorCode::NotFound, label("transceiver", id) + " does not exist");
    if (transceiverInUse(id))
        return err(ErrorCode::ConstraintViolation, label("transceiver", id) + " is attached to a node");
    transceivers_.erase(id);
    return {};
}

// ---------------------------------------------------------------------- lookup

const Node* Network::findNode(NodeId id) const noexcept { return findIn(nodes_, id); }
const FiberLink* Network::findLink(LinkId id) const noexcept { return findIn(links_, id); }
const Transceiver* Network::findTransceiver(TransceiverId id) const noexcept { return findIn(transceivers_, id); }
const SwitchingElement* Network::findSwitchingElement(SwitchingElementId id) const noexcept {
    return findIn(switchingElements_, id);
}

std::vector<LinkId> Network::linksOf(NodeId node) const {
    std::vector<LinkId> result;
    for (const auto& [id, link] : links_) {
        if (link.touches(node)) result.push_back(id);
    }
    return result;
}

// --------------------------------------------------------------------- private

bool Network::switchingElementInUse(SwitchingElementId id) const noexcept {
    return std::ranges::any_of(nodes_, [id](const auto& entry) { return entry.second.switchingElement() == id; });
}

bool Network::transceiverInUse(TransceiverId id) const noexcept {
    return std::ranges::any_of(nodes_, [id](const auto& entry) {
        const auto& list = entry.second.transceivers();
        return std::ranges::find(list, id) != list.end();
    });
}

}  // namespace opticalnet

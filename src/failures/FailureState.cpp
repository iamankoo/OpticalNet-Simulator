#include "opticalnet/failures/FailureState.hpp"

#include <algorithm>
#include <string>

namespace opticalnet {

namespace {

Error unknown(const char* what, std::uint32_t id) {
    return Error{ErrorCode::NotFound, std::string(what) + " " + std::to_string(id) + " does not exist"};
}

Error invalidTransition(const char* what, std::uint32_t id, const char* why) {
    return Error{ErrorCode::InvalidTransition, std::string(what) + " " + std::to_string(id) + " " + why};
}

}  // namespace

Result<void> FailureState::failLink(LinkId link) {
    if (!topology_->hasLink(link)) return unknown("link", link.value());
    if (!failedLinks_.insert(link).second) return invalidTransition("link", link.value(), "has already failed");
    return {};
}

Result<void> FailureState::recoverLink(LinkId link) {
    if (!topology_->hasLink(link)) return unknown("link", link.value());
    if (failedLinks_.erase(link) == 0) return invalidTransition("link", link.value(), "has not failed, so it cannot recover");
    return {};
}

Result<void> FailureState::failNode(NodeId node) {
    if (!topology_->hasNode(node)) return unknown("node", node.value());
    if (!failedNodes_.insert(node).second) return invalidTransition("node", node.value(), "has already failed");
    return {};
}

Result<void> FailureState::recoverNode(NodeId node) {
    if (!topology_->hasNode(node)) return unknown("node", node.value());
    if (failedNodes_.erase(node) == 0) return invalidTransition("node", node.value(), "has not failed, so it cannot recover");
    return {};
}

void FailureState::clearFailures() noexcept {
    failedLinks_.clear();
    failedNodes_.clear();
}

bool FailureState::isLinkUsable(const FiberLink& link) const noexcept {
    return !failedLinks_.contains(link.id()) && !failedNodes_.contains(link.source()) && !failedNodes_.contains(link.target());
}

bool FailureState::isPathAffected(const Path& path) const noexcept {
    if (!anyFailed()) return false;
    return std::ranges::any_of(path.links(), [this](LinkId l) { return failedLinks_.contains(l); }) ||
           std::ranges::any_of(path.nodes(), [this](NodeId n) { return failedNodes_.contains(n); });
}

}  // namespace opticalnet

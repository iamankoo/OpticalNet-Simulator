#include "opticalnet/resources/ResourceManager.hpp"

#include <string>

namespace opticalnet {

namespace {

std::string linkLabel(LinkId id) { return "link " + std::to_string(id.value()); }

}  // namespace

// ------------------------------------------------------------------- accounting

Result<LinkUsage> ResourceManager::usage(LinkId link) const {
    const FiberLink* fiber = topology_->findLink(link);
    if (fiber == nullptr) return Error{ErrorCode::NotFound, linkLabel(link) + " does not exist"};
    const std::uint32_t total = fiber->capacityChannels();
    const std::uint32_t used = allocated(link);
    return LinkUsage{total, used, used > total ? 0u : total - used};
}

bool ResourceManager::hasCapacity(const FiberLink& link, std::uint32_t demand) const noexcept {
    return static_cast<std::uint64_t>(allocated(link.id())) + demand <= link.capacityChannels();
}

std::uint32_t ResourceManager::allocated(LinkId link) const noexcept {
    const auto it = allocated_.find(link);
    return it == allocated_.end() ? 0u : it->second;
}

std::vector<std::pair<LinkId, std::uint32_t>> ResourceManager::allocations() const {
    return {allocated_.begin(), allocated_.end()};
}

std::vector<std::pair<LinkId, LinkUsage>> ResourceManager::usages() const {
    std::vector<std::pair<LinkId, LinkUsage>> result;
    result.reserve(topology_->linkCount());
    for (const auto& entry : topology_->network().links()) {
        result.emplace_back(entry.first, usage(entry.first).value());
    }
    return result;
}

std::uint64_t ResourceManager::totalCapacity() const noexcept {
    std::uint64_t sum = 0;
    for (const auto& entry : topology_->network().links()) sum += entry.second.capacityChannels();
    return sum;
}

std::uint64_t ResourceManager::totalAllocated() const noexcept {
    std::uint64_t sum = 0;
    for (const auto& entry : allocated_) sum += entry.second;
    return sum;
}

std::uint64_t ResourceManager::totalAvailable() const noexcept {
    std::uint64_t sum = 0;
    for (const auto& entry : topology_->network().links()) {
        const std::uint32_t total = entry.second.capacityChannels();
        const std::uint32_t used = allocated(entry.first);
        sum += used > total ? 0u : total - used;
    }
    return sum;
}

// ----------------------------------------------------------------- allocation

Result<std::map<LinkId, std::uint64_t>> ResourceManager::needs(std::span<const LinkId> links, std::uint32_t demand) {
    if (demand == 0) return Error{ErrorCode::InvalidArgument, "capacity demand must be at least one channel"};
    if (links.empty()) return Error{ErrorCode::InvalidArgument, "a path must contain at least one link"};
    std::map<LinkId, std::uint64_t> needed;
    for (const LinkId link : links) needed[link] += demand;
    return needed;
}

Result<void> ResourceManager::checkAllocation(const std::map<LinkId, std::uint64_t>& needed) const {
    for (const auto& [link, channels] : needed) {  // ascending LinkId: the first problem is reported
        const FiberLink* fiber = topology_->findLink(link);
        if (fiber == nullptr) return Error{ErrorCode::NotFound, linkLabel(link) + " does not exist"};
        const std::uint64_t used = allocated(link);
        const std::uint64_t total = fiber->capacityChannels();
        if (used + channels > total) {
            const std::uint64_t free = used > total ? 0 : total - used;
            return Error{ErrorCode::InsufficientCapacity,
                         linkLabel(link) + " has " + std::to_string(free) + " free channels, " + std::to_string(channels) +
                             " requested"};
        }
    }
    return {};
}

Result<void> ResourceManager::canAllocate(std::span<const LinkId> links, std::uint32_t demand) const {
    auto needed = needs(links, demand);
    if (!needed.ok()) return needed.error();
    return checkAllocation(needed.value());
}

Result<void> ResourceManager::allocate(std::span<const LinkId> links, std::uint32_t demand) {
    auto needed = needs(links, demand);
    if (!needed.ok()) return needed.error();
    // Step 1: every link is checked, nothing is modified yet.
    if (auto check = checkAllocation(needed.value()); !check.ok()) return check;
    // Step 2: all checks passed, so the updates below cannot fail.
    for (const auto& [link, channels] : needed.value()) {
        allocated_[link] += static_cast<std::uint32_t>(channels);  // <= total <= UINT32_MAX, checked above
    }
    return {};
}

Result<void> ResourceManager::release(std::span<const LinkId> links, std::uint32_t demand) {
    auto needed = needs(links, demand);
    if (!needed.ok()) return needed.error();
    for (const auto& [link, channels] : needed.value()) {
        if (allocated(link) < channels) {
            return Error{ErrorCode::InconsistentState, "cannot release " + std::to_string(channels) + " channels on " +
                                                           linkLabel(link) + ": only " + std::to_string(allocated(link)) +
                                                           " allocated"};
        }
    }
    for (const auto& [link, channels] : needed.value()) {
        const auto it = allocated_.find(link);
        it->second -= static_cast<std::uint32_t>(channels);
        if (it->second == 0) allocated_.erase(it);
    }
    return {};
}

// ---------------------------------------------------------------- consistency

ValidationReport ResourceManager::validate() const {
    ValidationReport report;
    for (const auto& [link, used] : allocated_) {
        const FiberLink* fiber = topology_->findLink(link);
        if (fiber == nullptr) {
            report.issues.push_back({Severity::Error, IssueCode::AllocationOnUnknownLink,
                                     std::to_string(used) + " channels allocated on " + linkLabel(link) +
                                         ", which is not in the topology"});
        } else if (used > fiber->capacityChannels()) {
            report.issues.push_back({Severity::Error, IssueCode::AllocationExceedsCapacity,
                                     linkLabel(link) + " has " + std::to_string(used) + " channels allocated but capacity " +
                                         std::to_string(fiber->capacityChannels())});
        }
    }
    return report;
}

}  // namespace opticalnet

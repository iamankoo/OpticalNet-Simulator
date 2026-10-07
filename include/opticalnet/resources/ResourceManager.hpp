#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// Capacity of one link, in channels. Invariant: allocated + available == total.
struct LinkUsage {
    std::uint32_t total = 0;      // configured: FiberLink::capacityChannels
    std::uint32_t allocated = 0;  // currently reserved by connections
    std::uint32_t available = 0;  // total - allocated
};

// Dynamic resource state: how many channels of each link are currently reserved.
//
//   Topology         = static structure (including each link's total capacity)
//   ResourceManager  = dynamic state (channels allocated per link)
//
// Only the *allocated* counts are stored here. Totals are read from the Topology on demand, so
// static capacity lives in exactly one place. The manager references the topology; it does not
// own it, and the topology must outlive the manager.
//
// Units: one connection of demand N uses N channels on every link of its path. The channels of
// a link are a single pool shared by both directions. Which wavelength a channel is (spectrum
// assignment) is not modelled.
//
// Atomicity: allocate() and release() validate every link first and change state only if all
// checks pass, so each call either fully succeeds or leaves the state untouched. A link that
// appears several times in the argument counts several times.
//
// Threading: single-threaded. All state sits behind this class's methods, so Phase 6 can add one
// lock around it (or around the owning ConnectionManager) without changing the API.
class ResourceManager {
public:
    explicit ResourceManager(const Topology& topology) : topology_(&topology) {}

    // --- per-link accounting ---
    // NotFound if the topology has no such link.
    [[nodiscard]] Result<LinkUsage> usage(LinkId link) const;
    // True if `demand` more channels fit on the link right now.
    [[nodiscard]] bool hasCapacity(const FiberLink& link, std::uint32_t demand) const noexcept;
    // Channels allocated on a link (0 if none). Does not require the link to exist in the topology.
    [[nodiscard]] std::uint32_t allocated(LinkId link) const noexcept;
    // Every link that has channels allocated, ascending by LinkId.
    [[nodiscard]] std::vector<std::pair<LinkId, std::uint32_t>> allocations() const;
    // Usage of every link in the topology, ascending by LinkId.
    [[nodiscard]] std::vector<std::pair<LinkId, LinkUsage>> usages() const;

    // --- network-wide accounting (sums over all links) ---
    [[nodiscard]] std::uint64_t totalCapacity() const noexcept;
    [[nodiscard]] std::uint64_t totalAllocated() const noexcept;
    [[nodiscard]] std::uint64_t totalAvailable() const noexcept;

    // --- allocation ---
    // Would allocate() succeed? Changes nothing.
    //   InvalidArgument: demand is 0 or `links` is empty
    //   NotFound: a link is not in the topology
    //   InsufficientCapacity: some link lacks `demand` free channels
    [[nodiscard]] Result<void> canAllocate(std::span<const LinkId> links, std::uint32_t demand) const;
    // Reserves `demand` channels on every link, atomically. Same errors as canAllocate().
    Result<void> allocate(std::span<const LinkId> links, std::uint32_t demand);
    // Returns exactly `demand` channels on every link, atomically.
    //   InvalidArgument: demand is 0 or `links` is empty
    //   InconsistentState: some link has fewer than `demand` channels allocated (nothing is changed)
    // Releasing does not require the link to still exist in the topology.
    Result<void> release(std::span<const LinkId> links, std::uint32_t demand);

    // --- consistency ---
    // Reports allocations that exceed a link's capacity or sit on links the topology no longer has.
    // (Negative allocated/available values cannot be represented: the counters are unsigned and
    // every operation is checked before it subtracts.)
    [[nodiscard]] ValidationReport validate() const;

private:
    // Per-link channel counts needed by a request, merged (a repeated link needs its demand repeatedly).
    [[nodiscard]] static Result<std::map<LinkId, std::uint64_t>> needs(std::span<const LinkId> links,
                                                                       std::uint32_t demand);
    [[nodiscard]] Result<void> checkAllocation(const std::map<LinkId, std::uint64_t>& needed) const;

    const Topology* topology_;
    std::map<LinkId, std::uint32_t> allocated_;  // only links with allocated > 0
};

}  // namespace opticalnet

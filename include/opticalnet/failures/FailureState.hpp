#pragma once

#include <set>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/FiberLink.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/routing/Path.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// Which links and nodes are currently failed: a NON-DESTRUCTIVE OVERLAY on an immutable Topology.
//
//   Immutable Topology  +  FailureState  =  current network availability
//
// A failed resource is UNAVAILABLE, not deleted: its id stays valid, the topology is never modified, its
// configured capacity is untouched, and recovering it simply makes it usable again.
//
//   link usable   <=>  the link is not failed AND neither of its end nodes is failed
//   node usable   <=>  the node is not failed
//
// so a failed node makes every link incident to it unusable (routes cannot enter, leave or pass through it)
// without any of those links being marked failed themselves.
//
// State transitions are explicit: failing a resource that is already failed, or recovering one that is not,
// is an error (InvalidTransition), and unknown ids are NotFound; failed operations change nothing.
//
// Ownership: one FailureState per simulation run (per replication); it references the topology, which must
// outlive it, and is not thread-safe (nothing shares it).
class FailureState {
public:
    explicit FailureState(const Topology& topology) : topology_(&topology) {}

    // --- transitions ---
    Result<void> failLink(LinkId link);
    Result<void> recoverLink(LinkId link);
    Result<void> failNode(NodeId node);
    Result<void> recoverNode(NodeId node);
    void clearFailures() noexcept;  // everything recovers at once

    // --- queries ---
    [[nodiscard]] bool isLinkFailed(LinkId link) const noexcept { return failedLinks_.contains(link); }
    [[nodiscard]] bool isNodeFailed(NodeId node) const noexcept { return failedNodes_.contains(node); }
    [[nodiscard]] bool isNodeUsable(NodeId node) const noexcept { return !isNodeFailed(node); }
    // Failed link or failed endpoint => unusable. This is the predicate routing uses.
    [[nodiscard]] bool isLinkUsable(const FiberLink& link) const noexcept;
    // True if the path uses a failed link or passes through (or starts/ends at) a failed node.
    [[nodiscard]] bool isPathAffected(const Path& path) const noexcept;

    // Ascending by id; read-only views.
    [[nodiscard]] const std::set<LinkId>& failedLinks() const noexcept { return failedLinks_; }
    [[nodiscard]] const std::set<NodeId>& failedNodes() const noexcept { return failedNodes_; }
    [[nodiscard]] bool anyFailed() const noexcept { return !failedLinks_.empty() || !failedNodes_.empty(); }

private:
    const Topology* topology_;
    std::set<LinkId> failedLinks_;
    std::set<NodeId> failedNodes_;
};

}  // namespace opticalnet

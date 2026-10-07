#pragma once

#include <cstddef>
#include <map>

#include "opticalnet/resources/Connection.hpp"
#include "opticalnet/resources/ResourceManager.hpp"
#include "opticalnet/routing/RoutingEngine.hpp"

namespace opticalnet {

// Connection lifecycle: establish, look up, release.
//
//   ConnectionRequest -> ConnectionManager -> RoutingEngine -> Path
//                                          -> ResourceManager (allocate) -> Connection
//
// Responsibilities stay separate:
//   Topology          static structure (referenced, not owned; must outlive the manager)
//   RoutingEngine     path calculation, stateless
//   ResourceManager   dynamic per-link capacity (owned here, readable through resources())
//   ConnectionManager the registry of active connections and their lifecycle
//
// Routing reaches the resource state only through RoutingConstraints::linkFilter ("does this
// link have enough free channels?"), so routing avoids full links without holding any resource
// state itself.
//
// Every operation is all-or-nothing: on any error no capacity is allocated or released and the
// registry is unchanged. There is no automatic expiry; connections live until release().
//
// Threading: single-threaded. Phase 6 can guard the whole manager with one lock.
class ConnectionManager {
public:
    explicit ConnectionManager(const Topology& topology, RoutingEngine routing = RoutingEngine{});

    // Route the request around links that lack free capacity, reserve the path, register it.
    //   InvalidArgument      bad request (zero capacity, source == destination)
    //   DuplicateId          the connection id is already active
    //   NotFound             unknown source or destination
    //   NoRoute              destination unreachable
    //   NoFeasibleRoute      routes exist but none satisfies the request's constraints
    //   InsufficientCapacity a route satisfying the constraints exists, but not with enough free channels
    [[nodiscard]] Result<Connection> establish(const ConnectionRequest& request);

    // Reserve a caller-supplied path (e.g. a precomputed backup) instead of routing. The path must
    // start/end at the request's nodes and be walkable link by link in the allowed direction.
    //   InvalidArgument      path does not match the request or is not contiguous/allowed by direction
    //   NotFound             path uses a link the topology does not have
    //   DuplicateId / InsufficientCapacity as above
    [[nodiscard]] Result<Connection> establishOnPath(const ConnectionRequest& request, const Path& path);

    // Return exactly the stored allocation and forget the connection. NotFound if not active
    // (including a second release).
    Result<void> release(ConnectionId id);

    // --- registry (read-only views) ---
    [[nodiscard]] const Connection* find(ConnectionId id) const noexcept;
    [[nodiscard]] bool contains(ConnectionId id) const noexcept { return find(id) != nullptr; }
    [[nodiscard]] std::size_t activeCount() const noexcept { return connections_.size(); }
    // Active connections, ascending by id.
    [[nodiscard]] const std::map<ConnectionId, Connection>& activeConnections() const noexcept {
        return connections_;
    }

    [[nodiscard]] const ResourceManager& resources() const noexcept { return resources_; }

    // ResourceManager::validate() plus a cross-check that the per-link allocation equals exactly
    // what the active connections hold.
    [[nodiscard]] ValidationReport validate() const;

private:
    [[nodiscard]] Result<void> checkPath(const ConnectionRequest& request, const Path& path) const;
    [[nodiscard]] Result<Connection> commit(const ConnectionRequest& request, Path path);

    const Topology* topology_;
    RoutingEngine routing_;
    ResourceManager resources_;
    std::map<ConnectionId, Connection> connections_;
};

}  // namespace opticalnet

#pragma once

#include <cstdint>
#include <vector>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/core/Ids.hpp"
#include "opticalnet/routing/Path.hpp"
#include "opticalnet/routing/RoutingConstraints.hpp"

namespace opticalnet {

// What a caller asks for. This is only an input to connection management, not a workload
// (request generation belongs to the simulation engine, Phase 5).
struct ConnectionRequest {
    ConnectionId id;
    NodeId source;
    NodeId destination;
    std::uint32_t capacityChannels;  // channels needed on every link of the path; must be > 0
    RoutingConstraints constraints{};  // metric, hop/cost limits, blocked elements, ...

    // Checks the request on its own (not against a topology):
    //   InvalidArgument: zero capacity, or source == destination
    [[nodiscard]] Result<void> validate() const;
};

// An established connection. It keeps the exact path it was given and the demand it reserved, so
// release never has to recompute a route (the topology may have changed and parallel links
// make the node sequence ambiguous). Created only by ConnectionManager; immutable.
class Connection {
public:
    [[nodiscard]] ConnectionId id() const noexcept { return request_.id; }
    [[nodiscard]] const ConnectionRequest& request() const noexcept { return request_; }
    [[nodiscard]] const Path& path() const noexcept { return path_; }
    [[nodiscard]] std::uint32_t capacityChannels() const noexcept { return request_.capacityChannels; }
    // The links holding this connection's allocation, in path order.
    [[nodiscard]] const std::vector<LinkId>& allocatedLinks() const noexcept { return path_.links(); }

private:
    friend class ConnectionManager;
    Connection(ConnectionRequest request, Path path) : request_(std::move(request)), path_(std::move(path)) {}

    ConnectionRequest request_;
    Path path_;
};

}  // namespace opticalnet

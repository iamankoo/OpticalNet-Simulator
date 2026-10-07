#include "opticalnet/resources/ConnectionManager.hpp"

#include <string>

namespace opticalnet {

namespace {

std::string idLabel(ConnectionId id) { return "connection " + std::to_string(id.value()); }

}  // namespace

Result<void> ConnectionRequest::validate() const {
    if (capacityChannels == 0)
        return Error{ErrorCode::InvalidArgument, idLabel(id) + ": requested capacity must be at least one channel"};
    if (source == destination)
        return Error{ErrorCode::InvalidArgument, idLabel(id) + ": source and destination must be different nodes"};
    return {};
}

ConnectionManager::ConnectionManager(const Topology& topology, RoutingEngine routing)
    : topology_(&topology), routing_(std::move(routing)), resources_(topology) {}

// -------------------------------------------------------------------- establish

Result<Connection> ConnectionManager::establish(const ConnectionRequest& request) {
    if (auto valid = request.validate(); !valid.ok()) return valid.error();
    if (connections_.contains(request.id))
        return Error{ErrorCode::DuplicateId, idLabel(request.id) + " is already active"};

    // Routing reads resource state only through the link filter: a link is usable if it has the
    // free channels this request needs (and passes the caller's own filter, if any).
    RoutingConstraints constraints = request.constraints;
    const std::uint32_t demand = request.capacityChannels;
    constraints.linkFilter = [this, demand, callerFilter = request.constraints.linkFilter](const FiberLink& link) {
        return resources_.hasCapacity(link, demand) && (!callerFilter || callerFilter(link));
    };

    auto path = routing_.findPath(*topology_, request.source, request.destination, constraints);
    if (!path.ok()) {
        if (path.error().code == ErrorCode::NoFeasibleRoute) {
            // Is capacity the only obstacle? Ask again without the capacity filter.
            RoutingConstraints withoutCapacity = request.constraints;
            if (routing_.findPath(*topology_, request.source, request.destination, withoutCapacity).ok()) {
                return Error{ErrorCode::InsufficientCapacity,
                             idLabel(request.id) + ": a route exists but no path has " + std::to_string(demand) +
                                 " free channels on every link"};
            }
        }
        return path.error();
    }
    return commit(request, std::move(path).value());
}

Result<Connection> ConnectionManager::establishOnPath(const ConnectionRequest& request, const Path& path) {
    if (auto valid = request.validate(); !valid.ok()) return valid.error();
    if (connections_.contains(request.id))
        return Error{ErrorCode::DuplicateId, idLabel(request.id) + " is already active"};
    if (auto ok = checkPath(request, path); !ok.ok()) return ok.error();
    return commit(request, path);
}

Result<void> ConnectionManager::checkPath(const ConnectionRequest& request, const Path& path) const {
    if (path.isTrivial())
        return Error{ErrorCode::InvalidArgument, idLabel(request.id) + ": a path without links cannot carry a connection"};
    if (path.source() != request.source || path.destination() != request.destination)
        return Error{ErrorCode::InvalidArgument, idLabel(request.id) + ": path endpoints do not match the request"};
    for (std::size_t i = 0; i < path.links().size(); ++i) {
        const LinkId linkId = path.links()[i];
        const FiberLink* link = topology_->findLink(linkId);
        if (link == nullptr)
            return Error{ErrorCode::NotFound, idLabel(request.id) + ": path uses link " + std::to_string(linkId.value()) +
                                                  ", which does not exist"};
        if (!topology_->hasNode(path.nodes()[i]) || !link->allowsTraversal(path.nodes()[i], path.nodes()[i + 1]))
            return Error{ErrorCode::InvalidArgument,
                         idLabel(request.id) + ": link " + std::to_string(linkId.value()) + " cannot be traversed from node " +
                             std::to_string(path.nodes()[i].value()) + " to node " + std::to_string(path.nodes()[i + 1].value())};
    }
    return {};
}

Result<Connection> ConnectionManager::commit(const ConnectionRequest& request, Path path) {
    // Allocate first: if it fails, nothing has changed and nothing is registered.
    if (auto allocated = resources_.allocate(path.links(), request.capacityChannels); !allocated.ok()) {
        return allocated.error();
    }
    Connection connection{request, std::move(path)};
    connections_.emplace(request.id, connection);
    return connection;
}

// ---------------------------------------------------------------------- release

Result<void> ConnectionManager::release(ConnectionId id) {
    const auto it = connections_.find(id);
    if (it == connections_.end()) return Error{ErrorCode::NotFound, idLabel(id) + " is not active"};
    // The stored links and demand are released, never a recomputed route.
    if (auto released = resources_.release(it->second.allocatedLinks(), it->second.capacityChannels()); !released.ok()) {
        return released;  // inconsistent bookkeeping: report it and keep the connection registered
    }
    connections_.erase(it);
    return {};
}

// ---------------------------------------------------------------------- queries

const Connection* ConnectionManager::find(ConnectionId id) const noexcept {
    const auto it = connections_.find(id);
    return it == connections_.end() ? nullptr : &it->second;
}

ValidationReport ConnectionManager::validate() const {
    ValidationReport report = resources_.validate();

    std::map<LinkId, std::uint64_t> expected;  // what the active connections hold, per link
    for (const auto& [id, connection] : connections_) {
        for (const LinkId link : connection.allocatedLinks()) expected[link] += connection.capacityChannels();
    }
    std::map<LinkId, std::uint64_t> actual;
    for (const auto& [link, channels] : resources_.allocations()) actual[link] = channels;

    for (const auto& [link, channels] : expected) {
        const auto it = actual.find(link);
        const std::uint64_t held = it == actual.end() ? 0 : it->second;
        if (held != channels) {
            report.issues.push_back({Severity::Error, IssueCode::ConnectionAllocationMismatch,
                                     "link " + std::to_string(link.value()) + ": connections hold " + std::to_string(channels) +
                                         " channels but " + std::to_string(held) + " are allocated"});
        }
    }
    for (const auto& [link, channels] : actual) {
        if (!expected.contains(link)) {
            report.issues.push_back({Severity::Error, IssueCode::ConnectionAllocationMismatch,
                                     "link " + std::to_string(link.value()) + ": " + std::to_string(channels) +
                                         " channels are allocated but no active connection uses the link"});
        }
    }
    return report;
}

}  // namespace opticalnet

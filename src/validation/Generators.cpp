#include "opticalnet/validation/Generators.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace opticalnet {

namespace {

bool chance(Rng& rng, double probability) {
    return rng.uniformInt(0, 999'999) < static_cast<std::uint32_t>(std::lround(probability * 1'000'000.0));
}

FiberLink makeLink(Rng& rng, const RandomTopologyConfig& cfg, std::uint32_t id, NodeId a, NodeId b) {
    if (chance(rng, 0.5)) std::swap(a, b);  // random orientation (matters for directed links)
    const auto direction = chance(rng, cfg.directedProbability) ? LinkDirection::Directed : LinkDirection::Bidirectional;
    return FiberLink::create(LinkId{id}, "l" + std::to_string(id), a, b, static_cast<double>(rng.uniformInt(1, cfg.maxLengthKm)), 0.2,
                             rng.uniformInt(cfg.minCapacity, cfg.maxCapacity), direction, static_cast<double>(rng.uniformInt(1, cfg.maxAdminCost)))
        .value();
}

}  // namespace

Topology generateRandomTopology(const RandomTopologyConfig& cfg, std::uint64_t seed) {
    if (cfg.minNodes < 2 || cfg.minNodes > cfg.maxNodes || cfg.minCapacity == 0 || cfg.minCapacity > cfg.maxCapacity)
        throw std::invalid_argument("generateRandomTopology: invalid configuration");
    Rng rng(seed);
    const std::uint32_t nodes = rng.uniformInt(cfg.minNodes, cfg.maxNodes);

    Topology t;
    for (std::uint32_t i = 1; i <= nodes; ++i) (void)t.addNode(Node::create(NodeId{i}, "n" + std::to_string(i)).value());

    std::uint32_t nextLink = 1;
    std::vector<std::pair<NodeId, NodeId>> pairs;
    for (std::uint32_t i = 2; i <= nodes; ++i) {  // spanning tree: node i hangs off a random earlier node
        const NodeId parent{static_cast<std::uint32_t>(rng.uniformIndex(i - 1)) + 1};
        const FiberLink link = makeLink(rng, cfg, nextLink++, parent, NodeId{i});
        (void)t.addLink(link);
        pairs.emplace_back(link.source(), link.target());
    }
    const auto extra = static_cast<std::uint32_t>(std::lround(cfg.extraLinksPerNode * nodes));
    for (std::uint32_t k = 0; k < extra; ++k) {
        NodeId a{0}, b{0};
        if (chance(rng, cfg.parallelProbability)) {
            const auto& pair = pairs[rng.uniformIndex(pairs.size())];
            a = pair.first;
            b = pair.second;
        } else {
            a = NodeId{static_cast<std::uint32_t>(rng.uniformIndex(nodes)) + 1};
            b = NodeId{static_cast<std::uint32_t>(rng.uniformIndex(nodes - 1)) + 1};
            if (b.value() >= a.value()) b = NodeId{b.value() + 1};
        }
        const FiberLink link = makeLink(rng, cfg, nextLink++, a, b);
        (void)t.addLink(link);
        pairs.emplace_back(link.source(), link.target());
    }

    if (cfg.attachEquipment) {
        for (std::uint32_t i = 1; i <= nodes; ++i) {
            (void)t.addSwitchingElement(SwitchingElement::create(SwitchingElementId{i}, "roadm" + std::to_string(i), SwitchingType::Roadm,
                                                                 static_cast<std::uint32_t>(t.degree(NodeId{i})) + rng.uniformInt(0, 2))
                                            .value());
            (void)t.attachSwitchingElement(NodeId{i}, SwitchingElementId{i});
            (void)t.addTransceiver(Transceiver::create(TransceiverId{i}, "trx" + std::to_string(i), 100.0, 5000.0).value());
            (void)t.attachTransceiver(NodeId{i}, TransceiverId{i});
        }
    }
    return t;
}

RoutingConstraints generateRandomConstraints(const Topology& topology, Rng& rng, double probability) {
    RoutingConstraints c;
    const CostMetric metrics[] = {CostMetric::HopCount, CostMetric::Distance, CostMetric::Administrative};
    c.metric = metrics[rng.uniformIndex(3)];
    if (chance(rng, probability)) c.maxHops = rng.uniformInt(1, 4);
    if (chance(rng, probability)) c.maxCost = static_cast<double>(rng.uniformInt(0, 40));
    if (chance(rng, probability / 2)) c.minLinkCapacityChannels = rng.uniformInt(1, 6);
    if (chance(rng, probability / 2) && topology.nodeCount() > 0)
        c.blockedNodes.insert(NodeId{static_cast<std::uint32_t>(rng.uniformIndex(topology.nodeCount())) + 1});
    if (chance(rng, probability / 2) && topology.linkCount() > 0)
        c.blockedLinks.insert(LinkId{static_cast<std::uint32_t>(rng.uniformIndex(topology.linkCount())) + 1});
    return c;
}

std::vector<ConnectionRequest> generateRandomRequests(const Topology& topology, const RandomRequestConfig& cfg, std::size_t count,
                                                      std::uint64_t seed) {
    if (topology.nodeCount() < 2 || cfg.minDemand == 0 || cfg.minDemand > cfg.maxDemand)
        throw std::invalid_argument("generateRandomRequests: invalid input");
    std::vector<NodeId> nodes;
    for (const auto& entry : topology.network().nodes()) nodes.push_back(entry.first);
    Rng rng(seed);
    std::vector<ConnectionRequest> requests;
    requests.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t s = rng.uniformIndex(nodes.size());
        std::size_t d = rng.uniformIndex(nodes.size() - 1);
        if (d >= s) ++d;
        RoutingConstraints constraints;
        if (chance(rng, cfg.constraintProbability)) constraints = generateRandomConstraints(topology, rng, 0.5);
        requests.push_back(ConnectionRequest{ConnectionId{static_cast<std::uint32_t>(i + 1)}, nodes[s], nodes[d],
                                             rng.uniformInt(cfg.minDemand, cfg.maxDemand), std::move(constraints)});
    }
    return requests;
}

std::vector<ScriptedFailure> generateRandomFailureScript(const Topology& topology, std::size_t operations, double endTime,
                                                         std::uint64_t seed) {
    std::vector<std::uint32_t> links, nodes;
    for (const auto& entry : topology.network().links()) links.push_back(entry.first.value());
    for (const auto& entry : topology.network().nodes()) nodes.push_back(entry.first.value());
    std::vector<ScriptedFailure> script;
    if (links.empty() && nodes.empty()) return script;
    Rng rng(seed);
    std::set<std::uint32_t> failedLinks, failedNodes;
    double time = 0.0;
    for (std::size_t i = 0; i < operations; ++i) {
        time += 1.0 + rng.uniformInt(0, 3);
        if (time > endTime) break;
        const bool node = !nodes.empty() && (links.empty() || chance(rng, 0.25));
        const std::uint32_t id = node ? nodes[rng.uniformIndex(nodes.size())] : links[rng.uniformIndex(links.size())];
        auto& failed = node ? failedNodes : failedLinks;
        if (failed.contains(id)) {
            failed.erase(id);
            script.push_back({time, node ? FailureAction::RecoverNode : FailureAction::RecoverLink, id});
        } else {
            failed.insert(id);
            script.push_back({time, node ? FailureAction::FailNode : FailureAction::FailLink, id});
        }
    }
    return script;
}

}  // namespace opticalnet

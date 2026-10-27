#include "opticalnet/simulation/RandomFailures.hpp"

#include <cmath>
#include <limits>

#include "opticalnet/simulation/Rng.hpp"

namespace opticalnet {

std::vector<ScriptedFailure> generateRandomFailures(const Topology& topology, const RandomFailureConfig& config, SimTime endTime,
                                                    std::uint64_t seed) {
    std::vector<ScriptedFailure> schedule;
    std::vector<std::uint32_t> links, nodes;
    for (const auto& entry : topology.network().links()) links.push_back(entry.first.value());  // ascending
    for (const auto& entry : topology.network().nodes()) nodes.push_back(entry.first.value());
    if (config.failureRate <= 0.0 || (links.empty() && nodes.empty())) return schedule;

    Rng rng(splitmix64(seed + 0xFA11FA11FA11FA11ULL));  // independent of the workload's stream
    std::vector<double> linkRepairAt(links.size(), 0.0), nodeRepairAt(nodes.size(), 0.0);  // up again at/after this time
    const double meanGap = 1.0 / config.failureRate;
    const auto nodeThreshold = static_cast<std::uint32_t>(std::lround(config.nodeFailureProbability * 1'000'000.0));

    double now = 0.0;
    for (;;) {
        double next = now + rng.exponential(meanGap);
        if (!(next > now)) next = std::nextafter(now, std::numeric_limits<double>::infinity());
        now = next;
        if (!(now <= endTime)) break;

        bool hitNode = false;
        if (!nodes.empty() && !links.empty()) hitNode = rng.uniformInt(0, 999'999) < nodeThreshold;
        else hitNode = links.empty();

        std::vector<std::size_t> healthy;
        const std::vector<double>& repairAt = hitNode ? nodeRepairAt : linkRepairAt;
        for (std::size_t i = 0; i < repairAt.size(); ++i) {
            if (repairAt[i] <= now) healthy.push_back(i);
        }
        if (healthy.empty()) continue;  // everything of that kind is already down

        const std::size_t pick = healthy[rng.uniformIndex(healthy.size())];
        double repair = now + rng.exponential(config.meanRepairTime);
        if (!(repair > now)) repair = std::nextafter(now, std::numeric_limits<double>::infinity());

        const std::uint32_t id = hitNode ? nodes[pick] : links[pick];
        schedule.push_back({now, hitNode ? FailureAction::FailNode : FailureAction::FailLink, id});
        (hitNode ? nodeRepairAt : linkRepairAt)[pick] = repair;
        if (repair <= endTime) schedule.push_back({repair, hitNode ? FailureAction::RecoverNode : FailureAction::RecoverLink, id});
    }
    return schedule;
}

}  // namespace opticalnet

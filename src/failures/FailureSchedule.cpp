#include "opticalnet/failures/FailureSchedule.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "opticalnet/failures/FailureState.hpp"

namespace opticalnet {

std::string_view toString(RecoveryPolicy policy) noexcept {
    switch (policy) {
        case RecoveryPolicy::NoRecovery: return "no_recovery";
        case RecoveryPolicy::Reroute: return "reroute";
    }
    return "unknown";
}

std::string_view toString(FailureAction action) noexcept {
    switch (action) {
        case FailureAction::FailLink: return "fail_link";
        case FailureAction::RecoverLink: return "recover_link";
        case FailureAction::FailNode: return "fail_node";
        case FailureAction::RecoverNode: return "recover_node";
    }
    return "unknown";
}

int failureActionPriority(FailureAction action) noexcept {
    switch (action) {
        case FailureAction::RecoverLink: return 0;
        case FailureAction::RecoverNode: return 1;
        case FailureAction::FailLink: return 2;
        case FailureAction::FailNode: return 3;
    }
    return 4;
}

Result<void> RandomFailureConfig::validate() const {
    const auto bad = [](std::string message) { return Result<void>{Error{ErrorCode::InvalidArgument, std::move(message)}}; };
    if (!(std::isfinite(failureRate) && failureRate >= 0.0)) return bad("failureRate must be a non-negative finite number");
    if (!(std::isfinite(meanRepairTime) && meanRepairTime > 0.0)) return bad("meanRepairTime must be a positive finite number");
    if (!(nodeFailureProbability >= 0.0 && nodeFailureProbability <= 1.0)) return bad("nodeFailureProbability must be between 0 and 1");
    return {};
}

Result<void> validateFailureSchedule(const Topology& topology, std::span<const ScriptedFailure> schedule) {
    for (const ScriptedFailure& f : schedule) {
        if (!std::isfinite(f.time) || f.time < 0.0)
            return Error{ErrorCode::InvalidArgument, "failure schedule time must be a non-negative finite number"};
    }
    std::vector<std::size_t> order(schedule.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&schedule](std::size_t a, std::size_t b) {
        if (schedule[a].time != schedule[b].time) return schedule[a].time < schedule[b].time;
        return failureActionPriority(schedule[a].action) < failureActionPriority(schedule[b].action);
    });

    FailureState scratch(topology);
    for (const std::size_t index : order) {
        const ScriptedFailure& f = schedule[index];
        Result<void> result = [&]() -> Result<void> {
            switch (f.action) {
                case FailureAction::FailLink: return scratch.failLink(LinkId{f.id});
                case FailureAction::RecoverLink: return scratch.recoverLink(LinkId{f.id});
                case FailureAction::FailNode: return scratch.failNode(NodeId{f.id});
                case FailureAction::RecoverNode: return scratch.recoverNode(NodeId{f.id});
            }
            return Error{ErrorCode::InvalidArgument, "unknown failure action"};
        }();
        if (!result.ok())
            return Error{result.error().code, "failure schedule entry " + std::to_string(index) + " (" + std::string(toString(f.action)) +
                                                  " " + std::to_string(f.id) + " at t=" + std::to_string(f.time) + "): " + result.error().message};
    }
    return {};
}

}  // namespace opticalnet

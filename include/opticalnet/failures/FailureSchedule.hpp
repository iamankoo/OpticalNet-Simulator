#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// What a connection does when a failure hits its path.
enum class RecoveryPolicy {
    NoRecovery,  // the connection is lost; its resources are released consistently
    Reroute,     // try to re-establish it (same id, demand and constraints) over the surviving network; lost if that fails
};

[[nodiscard]] std::string_view toString(RecoveryPolicy policy) noexcept;

enum class FailureAction { FailLink, RecoverLink, FailNode, RecoverNode };

[[nodiscard]] std::string_view toString(FailureAction action) noexcept;

// One entry of a scripted failure schedule: "at `time`, <action> link/node `id`" (time in simulated time units;
// `id` is a LinkId value for the link actions and a NodeId value for the node actions).
struct ScriptedFailure {
    double time;
    FailureAction action;
    std::uint32_t id;

    friend bool operator==(const ScriptedFailure&, const ScriptedFailure&) = default;
};

// A simple stochastic failure model (used only when the scripted schedule is empty).
//   failures arrive as a Poisson process of rate `failureRate` per time unit over the WHOLE network;
//   each failure hits a uniformly chosen currently-healthy resource: a node with probability
//   `nodeFailureProbability`, otherwise a link; the resource recovers after an exponentially distributed
//   repair time with mean `meanRepairTime` (a recovery beyond the simulation horizon is simply not generated).
// failureRate == 0 means no failures.
struct RandomFailureConfig {
    double failureRate = 0.0;
    double meanRepairTime = 10.0;
    double nodeFailureProbability = 0.0;  // in [0, 1]

    [[nodiscard]] Result<void> validate() const;
};

// Order in which scheduled failure actions that share a timestamp take effect (it matches the event queue):
// recoveries before failures, links before nodes. Lower value = earlier.
[[nodiscard]] int failureActionPriority(FailureAction action) noexcept;

// Checks a schedule against the topology by replaying it in event order (time, then failureActionPriority,
// then position in the list) on a scratch FailureState.
//   InvalidArgument  negative / NaN / infinite time
//   NotFound         unknown link or node
//   InvalidTransition  failing something already failed, or recovering something that is not failed
[[nodiscard]] Result<void> validateFailureSchedule(const Topology& topology, std::span<const ScriptedFailure> schedule);

}  // namespace opticalnet

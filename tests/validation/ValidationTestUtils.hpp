#pragma once

#include <gtest/gtest.h>

#include <string>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/simulation/SimulationEngine.hpp"
#include "opticalnet/simulation/SimulationStudy.hpp"
#include "opticalnet/topology/TopologyIo.hpp"
#include "opticalnet/validation/Generators.hpp"
#include "opticalnet/validation/Invariants.hpp"
#include "opticalnet/validation/ReferenceModels.hpp"

namespace opticalnet::testing {

// Turns a ValidationResult into a GoogleTest assertion whose failure message lists what the validator found.
inline ::testing::AssertionResult passes(const ValidationResult& result) {
    if (result.passed()) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << result.describe();
}
#define EXPECT_VALID(expr) EXPECT_TRUE(::opticalnet::testing::passes(expr))
#define ASSERT_VALID(expr) ASSERT_TRUE(::opticalnet::testing::passes(expr))

// "The validator must notice this": true if the named check failed.
#define EXPECT_DETECTED(result, checkName) \
    EXPECT_TRUE((result).failed(checkName)) << "validator missed '" << (checkName) << "'; it reported: " << ((result).passed() ? "nothing" : (result).describe())

// Loads tests/validation/fixtures/<name>.json.
inline Topology fixture(const std::string& name) {
    return must(loadTopologyFromFile(std::string(VALIDATION_FIXTURE_DIR) + "/" + name + ".json"));
}

// A complete structural fingerprint of a topology (nodes, links, equipment), for "this was not modified" checks.
inline std::string fingerprint(const Topology& t) { return topologyToJson(t, 0); }

// "seed 7" style context for randomized tests, so a failure can be reproduced.
inline std::string seedContext(std::uint64_t seed) { return "reproduce with seed " + std::to_string(seed); }

inline ScriptedArrival scripted(double time, std::uint32_t id, std::uint32_t from, std::uint32_t to, std::uint32_t demand, double lifetime,
                                RoutingConstraints constraints = {}) {
    return ScriptedArrival{time, ConnectionRequest{ConnectionId{id}, NodeId{from}, NodeId{to}, demand, std::move(constraints)}, lifetime};
}

inline SimulationConfig tracedConfig(double endTime, std::vector<ScriptedFailure> failures = {},
                                     RecoveryPolicy policy = RecoveryPolicy::Reroute) {
    SimulationConfig c;
    c.endTime = endTime;
    c.recordTrace = true;
    c.failureSchedule = std::move(failures);
    c.recoveryPolicy = policy;
    return c;
}

inline SimulationResult runScript(const Topology& t, const std::vector<ScriptedArrival>& script, const SimulationConfig& c) {
    return must(SimulationEngine{}.runScript(t, script, c));
}

}  // namespace opticalnet::testing

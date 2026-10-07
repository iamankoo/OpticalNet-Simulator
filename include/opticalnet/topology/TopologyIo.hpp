#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "opticalnet/core/Error.hpp"
#include "opticalnet/topology/Topology.hpp"

namespace opticalnet {

// JSON topology configuration.
//
// {
//   "transceivers":       [ {"id": 1, "name": "t", "data_rate_gbps": 100, "reach_km": 1000} ],
//   "switching_elements": [ {"id": 1, "name": "s", "type": "roadm"|"oxc", "ports": 8} ],
//   "nodes": [ {"id": 1, "name": "A", "switching_element": 1, "transceivers": [1]} ],
//   "links": [ {"id": 1, "name": "A-B", "source": 1, "target": 2,
//               "length_km": 100, "capacity_channels": 40,
//               "attenuation_db_per_km": 0.2, "direction": "bidirectional"|"directed",
//               "cost": 1.0} ]
// }
//
// Required: top-level "nodes"; node "id"; link "id", "source", "target", "length_km",
// "capacity_channels". Everything else is optional (names default to "node-<id>" /
// "link-<id>", attenuation 0.2 dB/km, direction bidirectional, cost 1).
// Ids are unsigned integers. Unknown keys are rejected so typos are not silently ignored.
//
// Errors: malformed JSON or schema violations -> ErrorCode::ParseError; domain rule
// violations keep their own code (InvalidArgument, DuplicateId, NotFound, ...). Every
// message names the offending location, e.g. "links[2]: ...".
[[nodiscard]] Result<Topology> loadTopologyFromJson(std::string_view jsonText);
[[nodiscard]] Result<Topology> loadTopologyFromFile(const std::filesystem::path& path);

// Serialises a topology in the schema above (all optional fields written explicitly).
// loadTopologyFromJson(topologyToJson(t)) reproduces t.
[[nodiscard]] std::string topologyToJson(const Topology& topology, int indent = 2);

}  // namespace opticalnet

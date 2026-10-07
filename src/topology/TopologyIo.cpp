#include "opticalnet/topology/TopologyIo.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>

namespace opticalnet {

namespace {

using Json = nlohmann::ordered_json;  // keeps field order stable in exported files

// Internal control flow only: thrown by the helpers below and converted back into a
// Result at the public API boundary. It never escapes this file.
struct LoadFailure {
    Error error;
};

[[noreturn]] void fail(ErrorCode code, const std::string& where, const std::string& message) {
    throw LoadFailure{Error{code, where + ": " + message}};
}

void check(const Result<void>& result, const std::string& where) {
    if (!result.ok()) fail(result.error().code, where, result.error().message);
}

template <class T>
T take(Result<T> result, const std::string& where) {
    if (!result.ok()) fail(result.error().code, where, result.error().message);
    return std::move(result).value();
}

std::string at(const char* array, std::size_t index) { return std::string(array) + "[" + std::to_string(index) + "]"; }

void requireObject(const Json& value, const std::string& where) {
    if (!value.is_object()) fail(ErrorCode::ParseError, where, "expected a JSON object");
}

void checkKeys(const Json& object, std::initializer_list<std::string_view> allowed, const std::string& where) {
    for (const auto& item : object.items()) {
        bool known = false;
        for (std::string_view key : allowed) known = known || key == item.key();
        if (!known) fail(ErrorCode::ParseError, where, "unknown field '" + item.key() + "'");
    }
}

const Json* field(const Json& object, const char* key, bool required, const std::string& where) {
    const auto it = object.find(key);
    if (it == object.end()) {
        if (required) fail(ErrorCode::ParseError, where, std::string("missing required field '") + key + "'");
        return nullptr;
    }
    return &*it;
}

std::uint32_t asUint32(const Json& value, const char* key, const std::string& where) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
        fail(ErrorCode::ParseError, where, std::string("field '") + key + "' must be an unsigned 32-bit integer");
    return value.get<std::uint32_t>();
}

double asNumber(const Json& value, const char* key, const std::string& where) {
    if (!value.is_number()) fail(ErrorCode::ParseError, where, std::string("field '") + key + "' must be a number");
    return value.get<double>();
}

std::string asString(const Json& value, const char* key, const std::string& where) {
    if (!value.is_string()) fail(ErrorCode::ParseError, where, std::string("field '") + key + "' must be a string");
    return value.get<std::string>();
}

std::uint32_t requiredUint(const Json& o, const char* key, const std::string& where) {
    return asUint32(*field(o, key, true, where), key, where);
}
double requiredNumber(const Json& o, const char* key, const std::string& where) {
    return asNumber(*field(o, key, true, where), key, where);
}
std::string requiredString(const Json& o, const char* key, const std::string& where) {
    return asString(*field(o, key, true, where), key, where);
}
double numberOr(const Json& o, const char* key, double fallback, const std::string& where) {
    const Json* f = field(o, key, false, where);
    return f ? asNumber(*f, key, where) : fallback;
}
std::string stringOr(const Json& o, const char* key, std::string fallback, const std::string& where) {
    const Json* f = field(o, key, false, where);
    return f ? asString(*f, key, where) : std::move(fallback);
}

const Json& arrayOf(const Json& root, const char* key, bool required, const std::string& where) {
    static const Json empty = Json::array();
    const Json* f = field(root, key, required, where);
    if (!f) return empty;
    if (!f->is_array()) fail(ErrorCode::ParseError, where, std::string("field '") + key + "' must be an array");
    return *f;
}

SwitchingType parseSwitchingType(const std::string& text, const std::string& where) {
    if (text == "roadm") return SwitchingType::Roadm;
    if (text == "oxc") return SwitchingType::Oxc;
    fail(ErrorCode::ParseError, where, "field 'type' must be \"roadm\" or \"oxc\", got \"" + text + "\"");
}

LinkDirection parseDirection(const std::string& text, const std::string& where) {
    if (text == "bidirectional") return LinkDirection::Bidirectional;
    if (text == "directed") return LinkDirection::Directed;
    fail(ErrorCode::ParseError, where,
         "field 'direction' must be \"bidirectional\" or \"directed\", got \"" + text + "\"");
}

std::string switchingTypeName(SwitchingType type) {
    return type == SwitchingType::Roadm ? "roadm" : "oxc";
}

Topology build(const Json& root) {
    requireObject(root, "topology");
    checkKeys(root, {"transceivers", "switching_elements", "nodes", "links"}, "topology");
    const Json& transceivers = arrayOf(root, "transceivers", false, "topology");
    const Json& switches = arrayOf(root, "switching_elements", false, "topology");
    const Json& nodes = arrayOf(root, "nodes", true, "topology");
    const Json& links = arrayOf(root, "links", false, "topology");

    Topology topology;

    for (std::size_t i = 0; i < transceivers.size(); ++i) {
        const std::string where = at("transceivers", i);
        const Json& o = transceivers[i];
        requireObject(o, where);
        checkKeys(o, {"id", "name", "data_rate_gbps", "reach_km"}, where);
        const auto id = requiredUint(o, "id", where);
        auto t = take(Transceiver::create(TransceiverId{id}, stringOr(o, "name", "transceiver-" + std::to_string(id), where),
                                          requiredNumber(o, "data_rate_gbps", where), requiredNumber(o, "reach_km", where)),
                      where);
        check(topology.addTransceiver(std::move(t)), where);
    }

    for (std::size_t i = 0; i < switches.size(); ++i) {
        const std::string where = at("switching_elements", i);
        const Json& o = switches[i];
        requireObject(o, where);
        checkKeys(o, {"id", "name", "type", "ports"}, where);
        const auto id = requiredUint(o, "id", where);
        auto s = take(SwitchingElement::create(SwitchingElementId{id},
                                               stringOr(o, "name", "switch-" + std::to_string(id), where),
                                               parseSwitchingType(requiredString(o, "type", where), where),
                                               requiredUint(o, "ports", where)),
                      where);
        check(topology.addSwitchingElement(std::move(s)), where);
    }

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::string where = at("nodes", i);
        const Json& o = nodes[i];
        requireObject(o, where);
        checkKeys(o, {"id", "name", "switching_element", "transceivers"}, where);
        const NodeId id{requiredUint(o, "id", where)};
        check(topology.addNode(take(Node::create(id, stringOr(o, "name", "node-" + std::to_string(id.value()), where)), where)),
              where);
        if (const Json* sw = field(o, "switching_element", false, where)) {
            check(topology.attachSwitchingElement(id, SwitchingElementId{asUint32(*sw, "switching_element", where)}), where);
        }
        if (const Json* list = field(o, "transceivers", false, where)) {
            if (!list->is_array()) fail(ErrorCode::ParseError, where, "field 'transceivers' must be an array");
            for (const Json& tid : *list) {
                check(topology.attachTransceiver(id, TransceiverId{asUint32(tid, "transceivers", where)}), where);
            }
        }
    }

    for (std::size_t i = 0; i < links.size(); ++i) {
        const std::string where = at("links", i);
        const Json& o = links[i];
        requireObject(o, where);
        checkKeys(o, {"id", "name", "source", "target", "length_km", "attenuation_db_per_km", "capacity_channels",
                      "direction", "cost"},
                  where);
        const auto id = requiredUint(o, "id", where);
        auto link = take(
            FiberLink::create(LinkId{id}, stringOr(o, "name", "link-" + std::to_string(id), where),
                              NodeId{requiredUint(o, "source", where)}, NodeId{requiredUint(o, "target", where)},
                              requiredNumber(o, "length_km", where), numberOr(o, "attenuation_db_per_km", 0.2, where),
                              requiredUint(o, "capacity_channels", where),
                              parseDirection(stringOr(o, "direction", "bidirectional", where), where),
                              numberOr(o, "cost", 1.0, where)),
            where);
        check(topology.addLink(std::move(link)), where);
    }
    return topology;
}

}  // namespace

Result<Topology> loadTopologyFromJson(std::string_view jsonText) {
    const Json root = Json::parse(jsonText.begin(), jsonText.end(), nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) return Error{ErrorCode::ParseError, "topology: malformed JSON"};
    try {
        return build(root);
    } catch (const LoadFailure& failure) {
        return failure.error;
    }
}

Result<Topology> loadTopologyFromFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{ErrorCode::NotFound, "cannot open topology file '" + path.string() + "'"};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return loadTopologyFromJson(buffer.str());
}

std::string topologyToJson(const Topology& topology, int indent) {
    const Network& net = topology.network();
    Json root = Json::object();

    root["transceivers"] = Json::array();
    for (const auto& [id, t] : net.transceivers()) {
        root["transceivers"].push_back(
            {{"id", id.value()}, {"name", t.name()}, {"data_rate_gbps", t.dataRateGbps()}, {"reach_km", t.reachKm()}});
    }
    root["switching_elements"] = Json::array();
    for (const auto& [id, s] : net.switchingElements()) {
        root["switching_elements"].push_back(
            {{"id", id.value()}, {"name", s.name()}, {"type", switchingTypeName(s.type())}, {"ports", s.portCount()}});
    }
    root["nodes"] = Json::array();
    for (const auto& [id, n] : net.nodes()) {
        Json node = {{"id", id.value()}, {"name", n.name()}};
        if (n.switchingElement()) node["switching_element"] = n.switchingElement()->value();
        if (!n.transceivers().empty()) {
            node["transceivers"] = Json::array();
            for (const TransceiverId t : n.transceivers()) node["transceivers"].push_back(t.value());
        }
        root["nodes"].push_back(std::move(node));
    }
    root["links"] = Json::array();
    for (const auto& [id, l] : net.links()) {
        root["links"].push_back({{"id", id.value()},
                                 {"name", l.name()},
                                 {"source", l.source().value()},
                                 {"target", l.target().value()},
                                 {"length_km", l.lengthKm()},
                                 {"attenuation_db_per_km", l.attenuationDbPerKm()},
                                 {"capacity_channels", l.capacityChannels()},
                                 {"direction", std::string(toString(l.direction()))},
                                 {"cost", l.administrativeCost()}});
    }
    return root.dump(indent);
}

}  // namespace opticalnet

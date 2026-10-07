#include <gtest/gtest.h>

#include <string>

#include "TestHelpers.hpp"
#include "opticalnet/topology/TopologyIo.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

const char* const kMinimal = R"({
  "nodes": [ {"id": 1, "name": "A"}, {"id": 2, "name": "B"}, {"id": 3} ],
  "links": [
    {"id": 1, "source": 1, "target": 2, "length_km": 100, "capacity_channels": 40},
    {"id": 2, "name": "B-C", "source": 2, "target": 3, "length_km": 50.5, "capacity_channels": 8,
     "attenuation_db_per_km": 0.25, "direction": "directed", "cost": 3.5}
  ]
})";

Result<Topology> load(const std::string& text) { return loadTopologyFromJson(text); }

// Replace the first occurrence of `from` in kMinimal.
std::string minimalWith(const std::string& from, const std::string& to) {
    std::string text = kMinimal;
    const auto pos = text.find(from);
    EXPECT_NE(pos, std::string::npos) << from;
    text.replace(pos, from.size(), to);
    return text;
}

}  // namespace

TEST(TopologyJson, LoadsValidTopologyWithDefaults) {
    const Topology t = must(load(kMinimal));
    EXPECT_EQ(t.nodeCount(), 3u);
    EXPECT_EQ(t.linkCount(), 2u);
    EXPECT_EQ(t.findNode(NodeId{1})->name(), "A");
    EXPECT_EQ(t.findNode(NodeId{3})->name(), "node-3") << "default name";

    const FiberLink& l1 = *t.findLink(LinkId{1});
    EXPECT_EQ(l1.name(), "link-1");
    EXPECT_EQ(l1.direction(), LinkDirection::Bidirectional);
    EXPECT_DOUBLE_EQ(l1.attenuationDbPerKm(), 0.2);
    EXPECT_DOUBLE_EQ(l1.administrativeCost(), 1.0);
    EXPECT_EQ(l1.capacityChannels(), 40u);

    const FiberLink& l2 = *t.findLink(LinkId{2});
    EXPECT_EQ(l2.name(), "B-C");
    EXPECT_EQ(l2.direction(), LinkDirection::Directed);
    EXPECT_DOUBLE_EQ(l2.lengthKm(), 50.5);
    EXPECT_DOUBLE_EQ(l2.attenuationDbPerKm(), 0.25);
    EXPECT_DOUBLE_EQ(l2.administrativeCost(), 3.5);
}

TEST(TopologyJson, LoadedTopologyHonoursDirectionInTheGraph) {
    const Topology t = must(load(kMinimal));
    EXPECT_TRUE(t.hasDirectLink(NodeId{2}, NodeId{1}));
    EXPECT_TRUE(t.hasDirectLink(NodeId{2}, NodeId{3}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{3}, NodeId{2}));
}

TEST(TopologyJson, EmptyTopologyIsAccepted) {
    const Topology t = must(load(R"({"nodes": []})"));
    EXPECT_EQ(t.nodeCount(), 0u);
    EXPECT_EQ(t.linkCount(), 0u);
}

TEST(TopologyJson, NodesOnlyIsAccepted) {
    const Topology t = must(load(R"({"nodes": [{"id": 1}, {"id": 2}]})"));
    EXPECT_EQ(t.nodeCount(), 2u);
    EXPECT_FALSE(t.isConnected());
}

TEST(TopologyJson, LoadsEquipmentAndAttachments) {
    const Topology t = must(load(R"({
      "transceivers": [{"id": 1, "name": "t", "data_rate_gbps": 400, "reach_km": 800}],
      "switching_elements": [{"id": 7, "type": "oxc", "ports": 16}],
      "nodes": [{"id": 1, "switching_element": 7, "transceivers": [1]}]
    })"));
    const Node& n = *t.findNode(NodeId{1});
    ASSERT_TRUE(n.switchingElement().has_value());
    EXPECT_EQ(*n.switchingElement(), SwitchingElementId{7});
    EXPECT_EQ(n.transceivers(), (std::vector<TransceiverId>{TransceiverId{1}}));
    EXPECT_EQ(t.network().findSwitchingElement(SwitchingElementId{7})->type(), SwitchingType::Oxc);
    EXPECT_EQ(t.network().findSwitchingElement(SwitchingElementId{7})->name(), "switch-7");
    EXPECT_DOUBLE_EQ(t.network().findTransceiver(TransceiverId{1})->dataRateGbps(), 400.0);
}

// ---------------------------------------------------------------- malformed

TEST(TopologyJson, MalformedJsonIsAParseError) {
    for (const char* text : {"", "{", "{\"nodes\": [}", "not json", "{\"nodes\": []} trailing"}) {
        ASSERT_ERROR(load(text), ErrorCode::ParseError) << text;
    }
}

TEST(TopologyJson, TopLevelMustBeAnObjectWithNodes) {
    ASSERT_ERROR(load("[]"), ErrorCode::ParseError);
    ASSERT_ERROR(load("42"), ErrorCode::ParseError);
    ASSERT_ERROR(load("{}"), ErrorCode::ParseError) << "nodes is required";
    ASSERT_ERROR(load(R"({"nodes": {}})"), ErrorCode::ParseError);
}

TEST(TopologyJson, UnknownFieldsAreRejected) {
    ASSERT_ERROR(load(R"({"nodes": [], "extra": 1})"), ErrorCode::ParseError);
    ASSERT_ERROR(load(R"({"nodes": [{"id": 1, "nmae": "typo"}]})"), ErrorCode::ParseError);
    ASSERT_ERROR(load(minimalWith("\"capacity_channels\": 40", "\"capacity_channels\": 40, \"bogus\": true")),
                 ErrorCode::ParseError);
}

// ------------------------------------------------------------ missing fields

TEST(TopologyJson, MissingRequiredNodeFieldIsRejected) {
    const auto r = load(R"({"nodes": [{"name": "A"}]})");
    ASSERT_ERROR(r, ErrorCode::ParseError);
    EXPECT_NE(r.error().message.find("nodes[0]"), std::string::npos) << "message locates the problem";
    EXPECT_NE(r.error().message.find("'id'"), std::string::npos);
}

TEST(TopologyJson, MissingRequiredLinkFieldsAreRejected) {
    for (const char* field : {"\"id\": 1, ", "\"source\": 1, ", "\"target\": 2, ", "\"length_km\": 100, ",
                              "\"capacity_channels\": 40"}) {
        std::string link = R"({"id": 1, "source": 1, "target": 2, "length_km": 100, "capacity_channels": 40})";
        const auto pos = link.find(field);
        ASSERT_NE(pos, std::string::npos) << field;
        link.erase(pos, std::string(field).size());
        if (std::string(field).find("capacity") != std::string::npos) link.erase(link.rfind(", "), 2);
        const std::string text = R"({"nodes": [{"id": 1}, {"id": 2}], "links": [)" + link + "]}";
        ASSERT_ERROR(load(text), ErrorCode::ParseError) << "without " << field;
    }
}

// ------------------------------------------------------------- wrong values

TEST(TopologyJson, WrongTypesAreRejected) {
    ASSERT_ERROR(load(R"({"nodes": [{"id": "A"}]})"), ErrorCode::ParseError) << "string id";
    ASSERT_ERROR(load(R"({"nodes": [{"id": -1}]})"), ErrorCode::ParseError) << "negative id";
    ASSERT_ERROR(load(R"({"nodes": [{"id": 1.5}]})"), ErrorCode::ParseError) << "fractional id";
    ASSERT_ERROR(load(R"({"nodes": [{"id": 4294967296}]})"), ErrorCode::ParseError) << "id overflows 32 bits";
    ASSERT_ERROR(load(R"({"nodes": [{"id": 1, "name": 5}]})"), ErrorCode::ParseError);
    ASSERT_ERROR(load(R"({"nodes": [1]})"), ErrorCode::ParseError) << "node is not an object";
    ASSERT_ERROR(load(minimalWith("\"length_km\": 100", "\"length_km\": \"far\"")), ErrorCode::ParseError);
    ASSERT_ERROR(load(minimalWith("\"direction\": \"directed\"", "\"direction\": \"sideways\"")), ErrorCode::ParseError);
    ASSERT_ERROR(load(R"({"nodes": [], "links": 3})"), ErrorCode::ParseError);
}

TEST(TopologyJson, InvalidLinkValuesKeepTheirDomainErrorCode) {
    ASSERT_ERROR(load(minimalWith("\"length_km\": 100", "\"length_km\": 0")), ErrorCode::InvalidArgument);
    ASSERT_ERROR(load(minimalWith("\"length_km\": 100", "\"length_km\": -5")), ErrorCode::InvalidArgument);
    ASSERT_ERROR(load(minimalWith("\"capacity_channels\": 40", "\"capacity_channels\": 0")), ErrorCode::InvalidArgument);
    ASSERT_ERROR(load(minimalWith("\"cost\": 3.5", "\"cost\": 0")), ErrorCode::InvalidArgument);
    ASSERT_ERROR(load(minimalWith("\"cost\": 3.5", "\"cost\": -1")), ErrorCode::InvalidArgument);
    ASSERT_ERROR(load(minimalWith("\"attenuation_db_per_km\": 0.25", "\"attenuation_db_per_km\": -0.1")),
                 ErrorCode::InvalidArgument);
}

TEST(TopologyJson, SelfLinkIsRejected) {
    const auto r = load(minimalWith("\"source\": 1, \"target\": 2", "\"source\": 1, \"target\": 1"));
    ASSERT_ERROR(r, ErrorCode::InvalidArgument);
    EXPECT_NE(r.error().message.find("links[0]"), std::string::npos);
}

TEST(TopologyJson, LinkToUnknownNodeIsRejected) {
    const auto r = load(minimalWith("\"target\": 2", "\"target\": 99"));
    ASSERT_ERROR(r, ErrorCode::NotFound);
    EXPECT_NE(r.error().message.find("99"), std::string::npos);
}

TEST(TopologyJson, DuplicateIdsAreRejected) {
    ASSERT_ERROR(load(R"({"nodes": [{"id": 1}, {"id": 1}]})"), ErrorCode::DuplicateId);
    ASSERT_ERROR(load(minimalWith("\"id\": 2, \"name\": \"B-C\"", "\"id\": 1, \"name\": \"B-C\"")), ErrorCode::DuplicateId);
    ASSERT_ERROR(load(R"({"transceivers": [
        {"id": 1, "data_rate_gbps": 100, "reach_km": 10}, {"id": 1, "data_rate_gbps": 100, "reach_km": 10}],
        "nodes": []})"),
                 ErrorCode::DuplicateId);
}

TEST(TopologyJson, ParallelLinksWithDistinctIdsAreAccepted) {
    const Topology t = must(load(R"({"nodes": [{"id": 1}, {"id": 2}], "links": [
        {"id": 1, "source": 1, "target": 2, "length_km": 10, "capacity_channels": 4},
        {"id": 2, "source": 1, "target": 2, "length_km": 12, "capacity_channels": 4}]})"));
    EXPECT_EQ(t.linksBetween(NodeId{1}, NodeId{2}).size(), 2u);
}

TEST(TopologyJson, BadEquipmentReferencesAreRejected) {
    ASSERT_ERROR(load(R"({"nodes": [{"id": 1, "switching_element": 5}]})"), ErrorCode::NotFound);
    ASSERT_ERROR(load(R"({"nodes": [{"id": 1, "transceivers": [5]}]})"), ErrorCode::NotFound);
    ASSERT_ERROR(load(R"({"switching_elements": [{"id": 1, "type": "bogus", "ports": 4}], "nodes": []})"),
                 ErrorCode::ParseError);
    ASSERT_ERROR(load(R"({"switching_elements": [{"id": 1, "type": "roadm", "ports": 0}], "nodes": []})"),
                 ErrorCode::InvalidArgument);
    // One switching element cannot serve two nodes.
    ASSERT_ERROR(load(R"({"switching_elements": [{"id": 1, "type": "roadm", "ports": 4}],
        "nodes": [{"id": 1, "switching_element": 1}, {"id": 2, "switching_element": 1}]})"),
                 ErrorCode::ConstraintViolation);
}

// --------------------------------------------------------------- files/export

TEST(TopologyJson, MissingFileIsNotFound) {
    ASSERT_ERROR(loadTopologyFromFile(std::string(OPTICALNET_CONFIG_DIR) + "/does-not-exist.json"), ErrorCode::NotFound);
}

TEST(TopologyJson, ExportRoundTripsExactly) {
    Topology original = must(load(kMinimal));
    ASSERT_OK(original.addTransceiver(makeTransceiver(4, 200.0, 750.0)));
    ASSERT_OK(original.addSwitchingElement(makeSwitch(2, 6)));
    ASSERT_OK(original.attachTransceiver(NodeId{1}, TransceiverId{4}));
    ASSERT_OK(original.attachSwitchingElement(NodeId{1}, SwitchingElementId{2}));

    const std::string once = topologyToJson(original);
    const Topology reloaded = must(load(once));
    EXPECT_EQ(topologyToJson(reloaded), once);
    EXPECT_EQ(reloaded.nodeCount(), original.nodeCount());
    EXPECT_EQ(reloaded.linkCount(), original.linkCount());
    EXPECT_EQ(reloaded.findLink(LinkId{2})->direction(), LinkDirection::Directed);
    EXPECT_DOUBLE_EQ(reloaded.findLink(LinkId{2})->administrativeCost(), 3.5);
    EXPECT_EQ(reloaded.findNode(NodeId{1})->transceivers().size(), 1u);
}

TEST(TopologyJson, ExportOfEmptyTopologyIsLoadable) {
    const Topology t = must(load(topologyToJson(Topology{})));
    EXPECT_EQ(t.nodeCount(), 0u);
}

// -------------------------------------------------------------- sample files

TEST(TopologyConfigs, Ring4LoadsAndValidates) {
    const Topology t = must(loadTopologyFromFile(std::string(OPTICALNET_CONFIG_DIR) + "/ring4.json"));
    EXPECT_EQ(t.nodeCount(), 4u);
    EXPECT_EQ(t.linkCount(), 4u);
    EXPECT_TRUE(t.isConnected());
    EXPECT_TRUE(t.isStronglyConnected());
    EXPECT_TRUE(t.validate().clean());
    EXPECT_EQ(t.neighbors(NodeId{1}), (std::vector<NodeId>{NodeId{2}, NodeId{4}}));
    EXPECT_DOUBLE_EQ(t.findLink(LinkId{3})->administrativeCost(), 3.0);
    EXPECT_EQ(t.findNode(NodeId{1})->transceivers().size(), 1u);
}

TEST(TopologyConfigs, NsfnetLoadsAndValidates) {
    const Topology t = must(loadTopologyFromFile(std::string(OPTICALNET_CONFIG_DIR) + "/nsfnet.json"));
    EXPECT_EQ(t.nodeCount(), 14u);
    EXPECT_EQ(t.linkCount(), 21u);
    EXPECT_TRUE(t.isConnected());
    EXPECT_TRUE(t.isStronglyConnected());
    EXPECT_TRUE(t.validate().clean());
    EXPECT_EQ(t.connectedComponents().size(), 1u);
    for (const auto& [id, link] : t.network().links()) EXPECT_GT(link.lengthKm(), 0.0);
}

#include <gtest/gtest.h>

#include "TestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(TopologyNodes, AddAndFind) {
    Topology t;
    ASSERT_OK(t.addNode(makeNode(1)));
    EXPECT_TRUE(t.hasNode(NodeId{1}));
    EXPECT_FALSE(t.hasNode(NodeId{2}));
    ASSERT_NE(t.findNode(NodeId{1}), nullptr);
    EXPECT_EQ(t.findNode(NodeId{1})->name(), "n1");
    EXPECT_EQ(t.findNode(NodeId{2}), nullptr);
    EXPECT_EQ(t.nodeCount(), 1u);
}

TEST(TopologyNodes, DuplicateNodeRejectedAndStateUnchanged) {
    Topology t = makeTopology({1}, {});
    ASSERT_ERROR(t.addNode(makeNode(1)), ErrorCode::DuplicateId);
    EXPECT_EQ(t.nodeCount(), 1u);
}

TEST(TopologyNodes, RemoveNodeWithoutLinks) {
    Topology t = makeTopology({1, 2}, {});
    ASSERT_OK(t.removeNode(NodeId{1}));
    EXPECT_FALSE(t.hasNode(NodeId{1}));
    EXPECT_EQ(t.nodeCount(), 1u);
    EXPECT_TRUE(t.outgoing(NodeId{1}).empty());
    EXPECT_TRUE(t.incident(NodeId{1}).empty());
}

TEST(TopologyNodes, RemoveNonexistentNode) {
    Topology t;
    ASSERT_ERROR(t.removeNode(NodeId{7}), ErrorCode::NotFound);
}

TEST(TopologyNodes, RemoveNodeWithLinksRejectedUntilLinksRemoved) {
    Topology t = makeTopology({1, 2}, {makeLink(1, 1, 2)});
    ASSERT_ERROR(t.removeNode(NodeId{1}), ErrorCode::ConstraintViolation);
    EXPECT_TRUE(t.hasNode(NodeId{1}));
    EXPECT_EQ(t.degree(NodeId{1}), 1u) << "failed removal must not disturb the index";
    ASSERT_OK(t.removeLink(LinkId{1}));
    ASSERT_OK(t.removeNode(NodeId{1}));
}

TEST(TopologyNodes, RemovedIdCanBeReused) {
    Topology t = makeTopology({1, 2}, {makeLink(1, 1, 2)});
    ASSERT_OK(t.removeLink(LinkId{1}));
    ASSERT_OK(t.removeNode(NodeId{1}));
    ASSERT_OK(t.addNode(makeNode(1)));
    ASSERT_OK(t.addLink(makeLink(1, 1, 2)));
    EXPECT_TRUE(t.hasDirectLink(NodeId{1}, NodeId{2}));
}

TEST(TopologyLinks, AddValidLinkAndFind) {
    Topology t = makeTopology({1, 2}, {});
    ASSERT_OK(t.addLink(makeLink(5, 1, 2, 250.0)));
    EXPECT_TRUE(t.hasLink(LinkId{5}));
    ASSERT_NE(t.findLink(LinkId{5}), nullptr);
    EXPECT_DOUBLE_EQ(t.findLink(LinkId{5})->lengthKm(), 250.0);
    EXPECT_EQ(t.findLink(LinkId{6}), nullptr);
    EXPECT_EQ(t.linkCount(), 1u);
}

TEST(TopologyLinks, UnknownEndpointRejectedWithoutSideEffects) {
    Topology t = makeTopology({1}, {});
    ASSERT_ERROR(t.addLink(makeLink(1, 1, 99)), ErrorCode::NotFound);
    ASSERT_ERROR(t.addLink(makeLink(1, 99, 1)), ErrorCode::NotFound);
    EXPECT_EQ(t.linkCount(), 0u);
    EXPECT_EQ(t.degree(NodeId{1}), 0u);
    EXPECT_TRUE(t.outgoing(NodeId{99}).empty()) << "no phantom entry for the unknown node";
}

TEST(TopologyLinks, SelfLinkIsRejectedByTheDomainModel) {
    const auto result = FiberLink::create(LinkId{1}, "loop", NodeId{1}, NodeId{1}, 10, 0.2, 10);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(TopologyLinks, DuplicateLinkIdRejected) {
    Topology t = makeTopology({1, 2, 3}, {makeLink(1, 1, 2)});
    ASSERT_ERROR(t.addLink(makeLink(1, 2, 3)), ErrorCode::DuplicateId);
    EXPECT_EQ(t.linkCount(), 1u);
    EXPECT_EQ(t.degree(NodeId{3}), 0u);
}

TEST(TopologyLinks, ParallelLinksWithDistinctIdsAreAllowed) {
    Topology t = makeTopology({1, 2}, {makeLink(1, 1, 2), makeLink(2, 1, 2), makeLink(3, 2, 1)});
    EXPECT_EQ(t.linkCount(), 3u);
    EXPECT_EQ(t.linksBetween(NodeId{1}, NodeId{2}), (std::vector<LinkId>{LinkId{1}, LinkId{2}, LinkId{3}}));
    EXPECT_EQ(t.neighbors(NodeId{1}), (std::vector<NodeId>{NodeId{2}})) << "neighbors are distinct nodes";
    EXPECT_EQ(t.degree(NodeId{1}), 3u);
}

TEST(TopologyLinks, RemoveLinkUpdatesAdjacencyOfBothEndpoints) {
    Topology t = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeLink(2, 2, 3)});
    ASSERT_OK(t.removeLink(LinkId{1}));
    EXPECT_FALSE(t.hasLink(LinkId{1}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{1}, NodeId{2}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{2}, NodeId{1}));
    EXPECT_TRUE(t.hasDirectLink(NodeId{2}, NodeId{3}));
    EXPECT_EQ(t.degree(NodeId{1}), 0u);
    EXPECT_EQ(t.degree(NodeId{2}), 1u);
}

TEST(TopologyLinks, RemoveOneOfParallelLinksKeepsTheOther) {
    Topology t = makeTopology({1, 2}, {makeLink(1, 1, 2), makeLink(2, 1, 2)});
    ASSERT_OK(t.removeLink(LinkId{1}));
    EXPECT_EQ(t.linksBetween(NodeId{1}, NodeId{2}), (std::vector<LinkId>{LinkId{2}}));
}

TEST(TopologyLinks, RemoveNonexistentLink) {
    Topology t = makeTopology({1, 2}, {});
    ASSERT_ERROR(t.removeLink(LinkId{3}), ErrorCode::NotFound);
}

TEST(TopologyLinks, RemoveDirectedLinkCleansUpBothIndexes) {
    Topology t = makeTopology({1, 2}, {makeDirectedLink(1, 1, 2)});
    ASSERT_OK(t.removeLink(LinkId{1}));
    EXPECT_TRUE(t.outgoing(NodeId{1}).empty());
    EXPECT_TRUE(t.incident(NodeId{2}).empty());
}

TEST(TopologyEquipment, EquipmentOperationsGoThroughTheSameRules) {
    Topology t = makeTopology({1, 2}, {});
    ASSERT_OK(t.addTransceiver(makeTransceiver(1)));
    ASSERT_OK(t.addSwitchingElement(makeSwitch(1)));
    ASSERT_OK(t.attachTransceiver(NodeId{1}, TransceiverId{1}));
    ASSERT_OK(t.attachSwitchingElement(NodeId{1}, SwitchingElementId{1}));
    ASSERT_ERROR(t.attachTransceiver(NodeId{2}, TransceiverId{1}), ErrorCode::ConstraintViolation);
    ASSERT_ERROR(t.removeTransceiver(TransceiverId{1}), ErrorCode::ConstraintViolation);
    ASSERT_ERROR(t.removeSwitchingElement(SwitchingElementId{1}), ErrorCode::ConstraintViolation);
    ASSERT_ERROR(t.addTransceiver(makeTransceiver(1)), ErrorCode::DuplicateId);
    ASSERT_ERROR(t.addSwitchingElement(makeSwitch(1)), ErrorCode::DuplicateId);
    EXPECT_EQ(t.network().transceivers().size(), 1u);
}

TEST(TopologyConstruction, FromExistingNetworkBuildsTheAdjacencyIndex) {
    Network net;
    for (std::uint32_t i = 1; i <= 3; ++i) ASSERT_OK(net.addNode(makeNode(i)));
    ASSERT_OK(net.addLink(makeLink(1, 1, 2)));
    ASSERT_OK(net.addLink(makeDirectedLink(2, 2, 3)));

    const Topology t{std::move(net)};
    EXPECT_EQ(t.nodeCount(), 3u);
    EXPECT_TRUE(t.hasDirectLink(NodeId{2}, NodeId{1}));
    EXPECT_TRUE(t.hasDirectLink(NodeId{2}, NodeId{3}));
    EXPECT_FALSE(t.hasDirectLink(NodeId{3}, NodeId{2}));
}

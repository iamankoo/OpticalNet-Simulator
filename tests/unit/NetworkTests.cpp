#include <gtest/gtest.h>

#include "TestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// 3 nodes in a line: 1 -(l1)- 2 -(l2)- 3, node 1 has a switch and a transceiver.
Network makeLineNetwork() {
    Network net;
    for (std::uint32_t i = 1; i <= 3; ++i) EXPECT_TRUE(net.addNode(makeNode(i)).ok());
    EXPECT_TRUE(net.addLink(makeLink(1, 1, 2)).ok());
    EXPECT_TRUE(net.addLink(makeLink(2, 2, 3)).ok());
    EXPECT_TRUE(net.addSwitchingElement(makeSwitch(1)).ok());
    EXPECT_TRUE(net.addTransceiver(makeTransceiver(1)).ok());
    EXPECT_TRUE(net.attachSwitchingElement(NodeId{1}, SwitchingElementId{1}).ok());
    EXPECT_TRUE(net.attachTransceiver(NodeId{1}, TransceiverId{1}).ok());
    return net;
}

}  // namespace

TEST(Network, StartsEmpty) {
    Network net;
    EXPECT_EQ(net.nodeCount(), 0u);
    EXPECT_EQ(net.linkCount(), 0u);
    EXPECT_TRUE(net.transceivers().empty());
    EXPECT_TRUE(net.switchingElements().empty());
    EXPECT_EQ(net.findNode(NodeId{1}), nullptr);
}

TEST(Network, RegistersAndFindsElements) {
    const Network net = makeLineNetwork();
    EXPECT_EQ(net.nodeCount(), 3u);
    EXPECT_EQ(net.linkCount(), 2u);
    ASSERT_NE(net.findNode(NodeId{2}), nullptr);
    EXPECT_EQ(net.findNode(NodeId{2})->name(), "n2");
    ASSERT_NE(net.findLink(LinkId{2}), nullptr);
    EXPECT_EQ(net.findLink(LinkId{2})->source(), NodeId{2});
    EXPECT_NE(net.findTransceiver(TransceiverId{1}), nullptr);
    EXPECT_NE(net.findSwitchingElement(SwitchingElementId{1}), nullptr);
    EXPECT_EQ(net.findLink(LinkId{99}), nullptr);
}

TEST(Network, RejectsDuplicateIds) {
    Network net = makeLineNetwork();
    ASSERT_ERROR(net.addNode(makeNode(1)), ErrorCode::DuplicateId);
    ASSERT_ERROR(net.addLink(makeLink(1, 2, 3)), ErrorCode::DuplicateId);
    ASSERT_ERROR(net.addTransceiver(makeTransceiver(1)), ErrorCode::DuplicateId);
    ASSERT_ERROR(net.addSwitchingElement(makeSwitch(1)), ErrorCode::DuplicateId);
    EXPECT_EQ(net.nodeCount(), 3u);
    EXPECT_EQ(net.linkCount(), 2u);
}

TEST(Network, IdsAreIndependentPerElementKind) {
    Network net;
    ASSERT_OK(net.addNode(makeNode(1)));
    ASSERT_OK(net.addTransceiver(makeTransceiver(1)));
    ASSERT_OK(net.addSwitchingElement(makeSwitch(1)));
}

TEST(Network, LinkRequiresExistingEndpoints) {
    Network net;
    ASSERT_OK(net.addNode(makeNode(1)));
    ASSERT_ERROR(net.addLink(makeLink(1, 1, 2)), ErrorCode::NotFound) << "target missing";
    ASSERT_ERROR(net.addLink(makeLink(1, 2, 1)), ErrorCode::NotFound) << "source missing";
    EXPECT_EQ(net.linkCount(), 0u);
}

TEST(Network, ParallelLinksBetweenSameNodesAreAllowed) {
    Network net = makeLineNetwork();
    ASSERT_OK(net.addLink(makeLink(10, 1, 2)));
    EXPECT_EQ(net.linksOf(NodeId{1}).size(), 2u);
}

TEST(Network, LinksOfReturnsTouchingLinksInIdOrder) {
    const Network net = makeLineNetwork();
    EXPECT_EQ(net.linksOf(NodeId{1}), (std::vector<LinkId>{LinkId{1}}));
    EXPECT_EQ(net.linksOf(NodeId{2}), (std::vector<LinkId>{LinkId{1}, LinkId{2}}));
    EXPECT_TRUE(net.linksOf(NodeId{99}).empty());
}

TEST(Network, AttachmentIsReflectedOnNode) {
    const Network net = makeLineNetwork();
    const Node* n = net.findNode(NodeId{1});
    ASSERT_NE(n, nullptr);
    ASSERT_TRUE(n->switchingElement().has_value());
    EXPECT_EQ(*n->switchingElement(), SwitchingElementId{1});
    ASSERT_EQ(n->transceivers().size(), 1u);
    EXPECT_EQ(n->transceivers()[0], TransceiverId{1});
}

TEST(Network, AttachmentRejectsMissingObjects) {
    Network net = makeLineNetwork();
    ASSERT_ERROR(net.attachSwitchingElement(NodeId{99}, SwitchingElementId{1}), ErrorCode::NotFound);
    ASSERT_ERROR(net.attachSwitchingElement(NodeId{2}, SwitchingElementId{99}), ErrorCode::NotFound);
    ASSERT_ERROR(net.attachTransceiver(NodeId{99}, TransceiverId{1}), ErrorCode::NotFound);
    ASSERT_ERROR(net.attachTransceiver(NodeId{2}, TransceiverId{99}), ErrorCode::NotFound);
}

TEST(Network, EquipmentBelongsToAtMostOneNode) {
    Network net = makeLineNetwork();
    ASSERT_ERROR(net.attachSwitchingElement(NodeId{2}, SwitchingElementId{1}), ErrorCode::ConstraintViolation);
    ASSERT_ERROR(net.attachTransceiver(NodeId{2}, TransceiverId{1}), ErrorCode::ConstraintViolation);
    ASSERT_ERROR(net.attachTransceiver(NodeId{1}, TransceiverId{1}), ErrorCode::ConstraintViolation) << "twice on same node";
}

TEST(Network, NodeHasAtMostOneSwitchingElement) {
    Network net = makeLineNetwork();
    ASSERT_OK(net.addSwitchingElement(makeSwitch(2)));
    ASSERT_ERROR(net.attachSwitchingElement(NodeId{1}, SwitchingElementId{2}), ErrorCode::ConstraintViolation);
}

TEST(Network, NodeCanHaveSeveralTransceivers) {
    Network net = makeLineNetwork();
    ASSERT_OK(net.addTransceiver(makeTransceiver(2)));
    ASSERT_OK(net.attachTransceiver(NodeId{1}, TransceiverId{2}));
    EXPECT_EQ(net.findNode(NodeId{1})->transceivers().size(), 2u);
}

TEST(Network, CannotRemoveNodeWithLinks) {
    Network net = makeLineNetwork();
    ASSERT_ERROR(net.removeNode(NodeId{2}), ErrorCode::ConstraintViolation);
    EXPECT_EQ(net.nodeCount(), 3u);
    ASSERT_OK(net.removeLink(LinkId{1}));
    ASSERT_OK(net.removeLink(LinkId{2}));
    ASSERT_OK(net.removeNode(NodeId{2}));
    EXPECT_EQ(net.nodeCount(), 2u);
    EXPECT_EQ(net.findNode(NodeId{2}), nullptr);
}

TEST(Network, CannotRemoveAttachedEquipmentUntilNodeIsGone) {
    Network net = makeLineNetwork();
    ASSERT_ERROR(net.removeSwitchingElement(SwitchingElementId{1}), ErrorCode::ConstraintViolation);
    ASSERT_ERROR(net.removeTransceiver(TransceiverId{1}), ErrorCode::ConstraintViolation);

    ASSERT_OK(net.removeLink(LinkId{1}));
    ASSERT_OK(net.removeNode(NodeId{1}));
    // Equipment is freed (still registered) and can now be removed or re-attached.
    ASSERT_OK(net.addNode(makeNode(1)));
    ASSERT_OK(net.attachTransceiver(NodeId{1}, TransceiverId{1}));
    ASSERT_OK(net.removeNode(NodeId{1}));
    ASSERT_OK(net.removeTransceiver(TransceiverId{1}));
    ASSERT_OK(net.removeSwitchingElement(SwitchingElementId{1}));
    EXPECT_TRUE(net.transceivers().empty());
    EXPECT_TRUE(net.switchingElements().empty());
}

TEST(Network, RemovingUnknownObjectsFails) {
    Network net;
    ASSERT_ERROR(net.removeNode(NodeId{1}), ErrorCode::NotFound);
    ASSERT_ERROR(net.removeLink(LinkId{1}), ErrorCode::NotFound);
    ASSERT_ERROR(net.removeTransceiver(TransceiverId{1}), ErrorCode::NotFound);
    ASSERT_ERROR(net.removeSwitchingElement(SwitchingElementId{1}), ErrorCode::NotFound);
}

TEST(Network, IterationIsOrderedById) {
    Network net;
    for (std::uint32_t id : {5u, 1u, 3u}) ASSERT_OK(net.addNode(makeNode(id)));
    std::vector<std::uint32_t> seen;
    for (const auto& [id, node] : net.nodes()) seen.push_back(id.value());
    EXPECT_EQ(seen, (std::vector<std::uint32_t>{1, 3, 5}));
}

TEST(Network, CopyIsIndependent) {
    Network a = makeLineNetwork();
    Network b = a;
    ASSERT_OK(b.removeLink(LinkId{1}));
    EXPECT_EQ(a.linkCount(), 2u);
    EXPECT_EQ(b.linkCount(), 1u);
}

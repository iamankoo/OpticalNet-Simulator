#include <gtest/gtest.h>

#include "TestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(Node, NewNodeHasNoEquipment) {
    const auto n = must(Node::create(NodeId{5}, "Delhi"));
    EXPECT_EQ(n.id(), NodeId{5});
    EXPECT_EQ(n.name(), "Delhi");
    EXPECT_EQ(n.kind(), ElementKind::Node);
    EXPECT_FALSE(n.switchingElement().has_value());
    EXPECT_TRUE(n.transceivers().empty());
}

TEST(Node, RejectsBlankName) {
    ASSERT_ERROR(Node::create(NodeId{1}, ""), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Node::create(NodeId{1}, " \t"), ErrorCode::InvalidArgument);
}

TEST(Node, CopiesAreIndependentValues) {
    Network net;
    ASSERT_OK(net.addNode(makeNode(1)));
    ASSERT_OK(net.addTransceiver(makeTransceiver(1)));
    ASSERT_OK(net.attachTransceiver(NodeId{1}, TransceiverId{1}));

    Node copy = *net.findNode(NodeId{1});
    EXPECT_EQ(copy.transceivers().size(), 1u);
    ASSERT_OK(net.addTransceiver(makeTransceiver(2)));
    ASSERT_OK(net.attachTransceiver(NodeId{1}, TransceiverId{2}));
    EXPECT_EQ(copy.transceivers().size(), 1u) << "copy must not see later attachments";
}

TEST(Node, ElementsAreUsableThroughTheCommonInterface) {
    const Node n = makeNode(1);
    const FiberLink l = makeLink(1, 1, 2);
    const Transceiver t = makeTransceiver(1);
    const SwitchingElement s = makeSwitch(1);
    const INetworkElement* all[] = {&n, &l, &t, &s};
    const ElementKind expected[] = {ElementKind::Node, ElementKind::FiberLink, ElementKind::Transceiver,
                                    ElementKind::SwitchingElement};
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(all[i]->kind(), expected[i]);
        EXPECT_FALSE(all[i]->name().empty());
        EXPECT_FALSE(all[i]->describe().empty());
    }
}

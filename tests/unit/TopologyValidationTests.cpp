#include <gtest/gtest.h>

#include "TestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(TopologyValidation, EmptyTopologyIsValidAndClean) {
    const auto report = Topology{}.validate();
    EXPECT_TRUE(report.valid());
    EXPECT_TRUE(report.clean());
}

TEST(TopologyValidation, ConnectedTopologyIsCleanlyValid) {
    const auto report = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeLink(2, 2, 3)}).validate();
    EXPECT_TRUE(report.valid());
    EXPECT_TRUE(report.clean());
    EXPECT_EQ(report.errorCount(), 0u);
}

TEST(TopologyValidation, SingleNodeIsNotReportedAsIsolated) {
    EXPECT_TRUE(makeTopology({1}, {}).validate().clean());
}

TEST(TopologyValidation, ValidIsNotTheSameAsConnected) {
    const Topology t = makeTopology({1, 2, 3, 4}, {makeLink(1, 1, 2), makeLink(2, 3, 4)});
    const auto report = t.validate();
    EXPECT_TRUE(report.valid()) << "a disconnected topology is still valid";
    EXPECT_FALSE(t.isConnected());
    EXPECT_FALSE(report.clean());
    EXPECT_TRUE(report.has(IssueCode::DisconnectedTopology));
    EXPECT_EQ(report.errorCount(), 0u);
    EXPECT_EQ(report.warningCount(), 1u);
}

TEST(TopologyValidation, IsolatedNodeIsAWarningNamingTheNode) {
    const auto report = makeTopology({1, 2, 3}, {makeLink(1, 1, 2)}).validate();
    EXPECT_TRUE(report.valid());
    ASSERT_TRUE(report.has(IssueCode::IsolatedNode));
    EXPECT_TRUE(report.has(IssueCode::DisconnectedTopology));
    for (const auto& issue : report.issues) {
        if (issue.code == IssueCode::IsolatedNode) {
            EXPECT_EQ(issue.severity, Severity::Warning);
            EXPECT_NE(issue.message.find("n3"), std::string::npos);
        }
    }
}

TEST(TopologyValidation, PortCountExceededIsAnError) {
    Topology t = makeTopology({1, 2, 3, 4}, {makeLink(1, 1, 2), makeLink(2, 1, 3), makeLink(3, 1, 4)});
    ASSERT_OK(t.addSwitchingElement(makeSwitch(1, 2)));  // only 2 ports, node 1 has 3 links
    ASSERT_OK(t.attachSwitchingElement(NodeId{1}, SwitchingElementId{1}));

    const auto report = t.validate();
    EXPECT_FALSE(report.valid());
    EXPECT_TRUE(report.has(IssueCode::PortCountExceeded));
    EXPECT_EQ(report.errorCount(), 1u);
}

TEST(TopologyValidation, PortCountBoundaryExactlyEnoughPortsIsFine) {
    Topology t = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeLink(2, 1, 3)});
    ASSERT_OK(t.addSwitchingElement(makeSwitch(1, 2)));
    ASSERT_OK(t.attachSwitchingElement(NodeId{1}, SwitchingElementId{1}));
    EXPECT_TRUE(t.validate().clean());
}

TEST(TopologyValidation, ParallelAndDirectedLinksEachConsumeAPort) {
    Topology t = makeTopology({1, 2}, {makeLink(1, 1, 2), makeLink(2, 1, 2), makeDirectedLink(3, 2, 1)});
    ASSERT_OK(t.addSwitchingElement(makeSwitch(1, 2)));
    ASSERT_OK(t.attachSwitchingElement(NodeId{2}, SwitchingElementId{1}));
    EXPECT_TRUE(t.validate().has(IssueCode::PortCountExceeded)) << "3 links, 2 ports";
}

TEST(TopologyValidation, NodesWithoutSwitchingElementAreNotPortChecked) {
    Topology t = makeTopology({1, 2, 3, 4}, {makeLink(1, 1, 2), makeLink(2, 1, 3), makeLink(3, 1, 4)});
    EXPECT_TRUE(t.validate().valid());
}

TEST(TopologyValidation, FixingTheTopologyClearsTheError) {
    Topology t = makeTopology({1, 2, 3}, {makeLink(1, 1, 2), makeLink(2, 1, 3)});
    ASSERT_OK(t.addSwitchingElement(makeSwitch(1, 1)));
    ASSERT_OK(t.attachSwitchingElement(NodeId{1}, SwitchingElementId{1}));
    ASSERT_FALSE(t.validate().valid());
    ASSERT_OK(t.removeLink(LinkId{2}));
    EXPECT_TRUE(t.validate().valid());
}

TEST(TopologyValidation, IssueCodesHaveNames) {
    EXPECT_EQ(toString(IssueCode::PortCountExceeded), "PortCountExceeded");
    EXPECT_EQ(toString(IssueCode::IsolatedNode), "IsolatedNode");
    EXPECT_EQ(toString(IssueCode::DisconnectedTopology), "DisconnectedTopology");
}

// Structural integrity is enforced at the mutation boundary, so these states can never
// be reached; the tests prove the guarantee rather than a report entry.
TEST(TopologyIntegrity, DanglingLinksCannotBeCreatedByAnyPath) {
    Topology t = makeTopology({1, 2}, {makeLink(1, 1, 2)});
    ASSERT_ERROR(t.addLink(makeLink(2, 1, 3)), ErrorCode::NotFound);
    ASSERT_ERROR(t.removeNode(NodeId{1}), ErrorCode::ConstraintViolation);
    for (const auto& [id, link] : t.network().links()) {
        EXPECT_TRUE(t.hasNode(link.source()));
        EXPECT_TRUE(t.hasNode(link.target()));
    }
}

TEST(TopologyIntegrity, InvalidCostsAndLengthsNeverReachTheTopology) {
    EXPECT_FALSE(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 100, 0.2, 10, LinkDirection::Bidirectional, -2).ok());
    EXPECT_FALSE(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, -100, 0.2, 10).ok());
}

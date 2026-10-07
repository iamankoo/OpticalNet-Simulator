#include <gtest/gtest.h>

#include <limits>

#include "TestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(FiberLink, StoresProperties) {
    const auto l = must(FiberLink::create(LinkId{9}, "A-B", NodeId{1}, NodeId{2}, 80.0, 0.25, 96));
    EXPECT_EQ(l.id(), LinkId{9});
    EXPECT_EQ(l.source(), NodeId{1});
    EXPECT_EQ(l.target(), NodeId{2});
    EXPECT_DOUBLE_EQ(l.lengthKm(), 80.0);
    EXPECT_DOUBLE_EQ(l.attenuationDbPerKm(), 0.25);
    EXPECT_EQ(l.capacityChannels(), 96u);
    EXPECT_EQ(l.kind(), ElementKind::FiberLink);
}

TEST(FiberLink, TotalLossIsLengthTimesAttenuation) {
    const auto l = must(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 80.0, 0.25, 10));
    EXPECT_DOUBLE_EQ(l.totalLossDb(), 20.0);
    const auto lossless = must(FiberLink::create(LinkId{2}, "l", NodeId{1}, NodeId{2}, 80.0, 0.0, 10));
    EXPECT_DOUBLE_EQ(lossless.totalLossDb(), 0.0);
}

TEST(FiberLink, TouchesOnlyItsEndpoints) {
    const auto l = makeLink(1, 1, 2);
    EXPECT_TRUE(l.touches(NodeId{1}));
    EXPECT_TRUE(l.touches(NodeId{2}));
    EXPECT_FALSE(l.touches(NodeId{3}));
}

TEST(FiberLink, RejectsInvalidState) {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto a = NodeId{1};
    const auto b = NodeId{2};
    ASSERT_ERROR(FiberLink::create(LinkId{1}, "", a, b, 10, 0.2, 10), ErrorCode::InvalidArgument);
    ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", a, a, 10, 0.2, 10), ErrorCode::InvalidArgument) << "self-loop";
    ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", a, b, 0, 0.2, 10), ErrorCode::InvalidArgument);
    ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", a, b, -5, 0.2, 10), ErrorCode::InvalidArgument);
    ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", a, b, nan, 0.2, 10), ErrorCode::InvalidArgument);
    ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", a, b, 10, -0.1, 10), ErrorCode::InvalidArgument);
    ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", a, b, 10, 0.2, 0), ErrorCode::InvalidArgument);
}

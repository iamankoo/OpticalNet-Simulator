#include <gtest/gtest.h>

#include "TestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(SwitchingElement, StoresProperties) {
    const auto s = must(SwitchingElement::create(SwitchingElementId{2}, "core", SwitchingType::Oxc, 16));
    EXPECT_EQ(s.id(), SwitchingElementId{2});
    EXPECT_EQ(s.type(), SwitchingType::Oxc);
    EXPECT_EQ(s.portCount(), 16u);
    EXPECT_EQ(s.kind(), ElementKind::SwitchingElement);
    EXPECT_NE(s.describe().find("OXC"), std::string::npos);
}

TEST(SwitchingElement, PortRangeIsZeroBased) {
    const auto s = makeSwitch(1, 4);
    EXPECT_TRUE(s.hasPort(0));
    EXPECT_TRUE(s.hasPort(3));
    EXPECT_FALSE(s.hasPort(4));
}

TEST(SwitchingElement, CanSwitchRequiresDistinctValidPorts) {
    const auto s = makeSwitch(1, 4);
    EXPECT_TRUE(s.canSwitch(0, 3));
    EXPECT_TRUE(s.canSwitch(3, 0));
    EXPECT_FALSE(s.canSwitch(2, 2)) << "loopback is not allowed";
    EXPECT_FALSE(s.canSwitch(0, 4)) << "output port out of range";
    EXPECT_FALSE(s.canSwitch(9, 1)) << "input port out of range";
}

TEST(SwitchingElement, SinglePortElementCannotSwitchAnything) {
    const auto s = makeSwitch(1, 1);
    EXPECT_FALSE(s.canSwitch(0, 0));
}

TEST(SwitchingElement, RejectsInvalidState) {
    ASSERT_ERROR(SwitchingElement::create(SwitchingElementId{1}, "", SwitchingType::Roadm, 4), ErrorCode::InvalidArgument);
    ASSERT_ERROR(SwitchingElement::create(SwitchingElementId{1}, "s", SwitchingType::Roadm, 0), ErrorCode::InvalidArgument);
}

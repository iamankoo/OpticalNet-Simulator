#include <gtest/gtest.h>

#include <limits>

#include "TestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(Transceiver, StoresProperties) {
    const auto t = makeTransceiver(3, 400.0, 600.0);
    EXPECT_EQ(t.id(), TransceiverId{3});
    EXPECT_EQ(t.name(), "t3");
    EXPECT_EQ(t.kind(), ElementKind::Transceiver);
    EXPECT_DOUBLE_EQ(t.dataRateGbps(), 400.0);
    EXPECT_DOUBLE_EQ(t.reachKm(), 600.0);
    EXPECT_NE(t.describe().find("400"), std::string::npos);
}

TEST(Transceiver, ReachIsInclusiveAndRejectsNegativeDistance) {
    const auto t = makeTransceiver(1, 100.0, 500.0);
    EXPECT_TRUE(t.canReach(0.0));
    EXPECT_TRUE(t.canReach(499.9));
    EXPECT_TRUE(t.canReach(500.0));
    EXPECT_FALSE(t.canReach(500.1));
    EXPECT_FALSE(t.canReach(-1.0));
}

TEST(Transceiver, RateSupportBoundary) {
    const auto t = makeTransceiver(1, 100.0, 500.0);
    EXPECT_TRUE(t.supportsRate(10.0));
    EXPECT_TRUE(t.supportsRate(100.0));
    EXPECT_FALSE(t.supportsRate(100.5));
    EXPECT_FALSE(t.supportsRate(-5.0));
}

TEST(Transceiver, RejectsInvalidState) {
    const auto inf = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "", 100, 100), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "   ", 100, 100), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "t", 0, 100), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "t", -10, 100), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "t", nan, 100), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "t", 100, 0), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "t", 100, inf), ErrorCode::InvalidArgument);
}

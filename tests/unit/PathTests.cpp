#include <gtest/gtest.h>

#include <limits>

#include "RoutingTestHelpers.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(Path, StoresNodesLinksCostAndHops) {
    const Path p = must(Path::create(nodeIds({1, 2, 3, 4}), linkIds({10, 11, 12}), 25.0, CostMetric::Distance));
    EXPECT_EQ(p.source(), NodeId{1});
    EXPECT_EQ(p.destination(), NodeId{4});
    EXPECT_EQ(p.nodes(), nodeIds({1, 2, 3, 4}));
    EXPECT_EQ(p.links(), linkIds({10, 11, 12}));
    EXPECT_DOUBLE_EQ(p.cost(), 25.0);
    EXPECT_EQ(p.hopCount(), 3u);
    EXPECT_EQ(p.metric(), CostMetric::Distance);
    EXPECT_FALSE(p.isTrivial());
}

TEST(Path, TrivialPathIsOneNodeNoLinksZeroCost) {
    const Path p = must(Path::create(nodeIds({7}), {}, 0.0, CostMetric::HopCount));
    EXPECT_TRUE(p.isTrivial());
    EXPECT_EQ(p.source(), NodeId{7});
    EXPECT_EQ(p.destination(), NodeId{7});
    EXPECT_EQ(p.hopCount(), 0u);
}

TEST(Path, RejectsInconsistentShape) {
    ASSERT_ERROR(Path::create({}, {}, 0.0, CostMetric::HopCount), ErrorCode::InvalidArgument) << "no nodes";
    ASSERT_ERROR(Path::create(nodeIds({1, 2}), {}, 1.0, CostMetric::HopCount), ErrorCode::InvalidArgument) << "link missing";
    ASSERT_ERROR(Path::create(nodeIds({1}), linkIds({1}), 1.0, CostMetric::HopCount), ErrorCode::InvalidArgument)
        << "extra link";
}

TEST(Path, RejectsInvalidCost) {
    ASSERT_ERROR(Path::create(nodeIds({1, 2}), linkIds({1}), -1.0, CostMetric::Distance), ErrorCode::InvalidArgument);
    ASSERT_ERROR(Path::create(nodeIds({1, 2}), linkIds({1}), std::numeric_limits<double>::quiet_NaN(), CostMetric::Distance),
                 ErrorCode::InvalidArgument);
    ASSERT_ERROR(Path::create(nodeIds({1}), {}, 5.0, CostMetric::Distance), ErrorCode::InvalidArgument)
        << "a path without links must cost 0";
}

TEST(Path, EqualityComparesEverything) {
    const auto make = [](double cost, std::uint32_t link) {
        return must(Path::create(nodeIds({1, 2}), linkIds({link}), cost, CostMetric::Distance));
    };
    EXPECT_EQ(make(5, 1), make(5, 1));
    EXPECT_NE(make(5, 1), make(6, 1));
    EXPECT_NE(make(5, 1), make(5, 2)) << "parallel links make different paths over the same nodes";
}

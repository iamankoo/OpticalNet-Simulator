#include <gtest/gtest.h>

#include <type_traits>
#include <unordered_set>

#include "opticalnet/core/Ids.hpp"

using namespace opticalnet;

TEST(StrongId, ComparesByValue) {
    EXPECT_EQ(NodeId{1}, NodeId{1});
    EXPECT_NE(NodeId{1}, NodeId{2});
    EXPECT_LT(NodeId{1}, NodeId{2});
    EXPECT_EQ(NodeId{7}.value(), 7u);
}

TEST(StrongId, DifferentKindsAreDistinctTypes) {
    static_assert(!std::is_same_v<NodeId, LinkId>);
    static_assert(!std::is_convertible_v<NodeId, LinkId>);
    static_assert(!std::is_convertible_v<unsigned, NodeId>, "construction must be explicit");
    SUCCEED();
}

TEST(StrongId, IsHashable) {
    std::unordered_set<NodeId> set{NodeId{1}, NodeId{2}, NodeId{1}};
    EXPECT_EQ(set.size(), 2u);
}

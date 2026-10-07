#include <gtest/gtest.h>

#include <map>
#include <random>

#include "RoutingTestHelpers.hpp"
#include "opticalnet/resources/ResourceManager.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// 1 --l1(100)-- 2 --l2(100)-- 3 --l3(50)-- 4   plus parallel l4(60) and l5(60) between 1 and 4.
Topology fixture() {
    return makeTopology({1, 2, 3, 4}, {routedLink(1, 1, 2, 1, 1, LinkDirection::Bidirectional, 100),
                                       routedLink(2, 2, 3, 1, 1, LinkDirection::Bidirectional, 100),
                                       routedLink(3, 3, 4, 1, 1, LinkDirection::Bidirectional, 50),
                                       routedLink(4, 1, 4, 1, 1, LinkDirection::Bidirectional, 60),
                                       routedLink(5, 1, 4, 1, 1, LinkDirection::Bidirectional, 60)});
}

LinkUsage usageOf(const ResourceManager& rm, std::uint32_t link) { return must(rm.usage(LinkId{link})); }

// allocated + available == total on every link, and the network-wide sums agree.
void expectInvariants(const ResourceManager& rm) {
    std::uint64_t total = 0, used = 0, free = 0;
    for (const auto& [id, u] : rm.usages()) {
        EXPECT_EQ(u.allocated + u.available, u.total) << "link " << id;
        EXPECT_LE(u.allocated, u.total) << "link " << id;
        total += u.total;
        used += u.allocated;
        free += u.available;
    }
    EXPECT_EQ(rm.totalCapacity(), total);
    EXPECT_EQ(rm.totalAllocated(), used);
    EXPECT_EQ(rm.totalAvailable(), free);
    EXPECT_EQ(rm.totalAllocated() + rm.totalAvailable(), rm.totalCapacity());
    EXPECT_TRUE(rm.validate().clean());
}

}  // namespace

TEST(ResourceManager, FreshManagerHasEverythingAvailable) {
    const Topology t = fixture();
    const ResourceManager rm{t};
    EXPECT_EQ(usageOf(rm, 1).total, 100u);
    EXPECT_EQ(usageOf(rm, 1).allocated, 0u);
    EXPECT_EQ(usageOf(rm, 1).available, 100u);
    EXPECT_EQ(rm.totalCapacity(), 100u + 100 + 50 + 60 + 60);
    EXPECT_EQ(rm.totalAllocated(), 0u);
    EXPECT_EQ(rm.totalAvailable(), rm.totalCapacity());
    EXPECT_TRUE(rm.allocations().empty());
    expectInvariants(rm);
}

TEST(ResourceManager, UnknownLinkIsNotFound) {
    const Topology t = fixture();
    const ResourceManager rm{t};
    ASSERT_ERROR(rm.usage(LinkId{99}), ErrorCode::NotFound);
}

TEST(ResourceManager, SingleLinkAllocation) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 30));
    EXPECT_EQ(usageOf(rm, 1).allocated, 30u);
    EXPECT_EQ(usageOf(rm, 1).available, 70u);
    EXPECT_EQ(usageOf(rm, 2).allocated, 0u) << "other links untouched";
    EXPECT_EQ(rm.totalAllocated(), 30u);
    expectInvariants(rm);
}

TEST(ResourceManager, MultiLinkAllocationChargesEveryLink) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1, 2, 3}), 20));
    for (std::uint32_t l : {1u, 2u, 3u}) EXPECT_EQ(usageOf(rm, l).allocated, 20u) << l;
    EXPECT_EQ(usageOf(rm, 3).available, 30u);
    EXPECT_EQ(rm.totalAllocated(), 60u);
    expectInvariants(rm);
}

TEST(ResourceManager, AllocatingExactlyTheCapacityIsAllowedOneMoreIsNot) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({3}), 50));
    EXPECT_EQ(usageOf(rm, 3).available, 0u);
    ASSERT_ERROR(rm.allocate(linkIds({3}), 1), ErrorCode::InsufficientCapacity);
    EXPECT_EQ(usageOf(rm, 3).allocated, 50u);
    expectInvariants(rm);
}

TEST(ResourceManager, DemandLargerThanCapacityIsRejected) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_ERROR(rm.allocate(linkIds({1}), 101), ErrorCode::InsufficientCapacity);
    ASSERT_ERROR(rm.allocate(linkIds({1}), 4'000'000'000u), ErrorCode::InsufficientCapacity) << "no overflow";
    EXPECT_EQ(rm.totalAllocated(), 0u);
}

TEST(ResourceManager, ThreeConnectionsFillALinkExactly) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 30));
    ASSERT_OK(rm.allocate(linkIds({1}), 40));
    ASSERT_OK(rm.allocate(linkIds({1}), 30));
    EXPECT_EQ(usageOf(rm, 1).allocated, 100u);
    EXPECT_EQ(usageOf(rm, 1).available, 0u);
    ASSERT_ERROR(rm.allocate(linkIds({1}), 1), ErrorCode::InsufficientCapacity);
    expectInvariants(rm);
}

// ------------------------------------------------------------------ atomicity

TEST(ResourceManagerAtomicity, InsufficientCapacityOnTheLastLinkAllocatesNothing) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({3}), 40));  // link 3 now has only 10 free
    const auto before = rm.allocations();

    // Path 1-2-3: links 1 and 2 have room for 20, the final link 3 does not.
    ASSERT_ERROR(rm.allocate(linkIds({1, 2, 3}), 20), ErrorCode::InsufficientCapacity);

    EXPECT_EQ(rm.allocations(), before) << "no partial allocation on links 1 and 2";
    EXPECT_EQ(usageOf(rm, 1).allocated, 0u);
    EXPECT_EQ(usageOf(rm, 2).allocated, 0u);
    EXPECT_EQ(usageOf(rm, 3).allocated, 40u);
    expectInvariants(rm);
}

TEST(ResourceManagerAtomicity, InsufficientCapacityOnTheFirstLinkAllocatesNothing) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 95));
    ASSERT_ERROR(rm.allocate(linkIds({1, 2, 3}), 20), ErrorCode::InsufficientCapacity);
    EXPECT_EQ(rm.totalAllocated(), 95u);
}

TEST(ResourceManagerAtomicity, UnknownLinkInTheMiddleAllocatesNothing) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_ERROR(rm.allocate(linkIds({1, 99, 2}), 10), ErrorCode::NotFound);
    EXPECT_EQ(rm.totalAllocated(), 0u);
    EXPECT_TRUE(rm.allocations().empty());
}

TEST(ResourceManagerAtomicity, CanAllocateChangesNothingAndAgreesWithAllocate) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.canAllocate(linkIds({1, 2, 3}), 50));
    EXPECT_EQ(rm.totalAllocated(), 0u);
    ASSERT_ERROR(rm.canAllocate(linkIds({1, 2, 3}), 51), ErrorCode::InsufficientCapacity);
    ASSERT_ERROR(rm.canAllocate(linkIds({1, 99}), 1), ErrorCode::NotFound);
    ASSERT_OK(rm.allocate(linkIds({1, 2, 3}), 50));
}

TEST(ResourceManagerAtomicity, RepeatedLinkInOneCallCountsRepeatedly) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_ERROR(rm.allocate(linkIds({3, 3}), 30), ErrorCode::InsufficientCapacity) << "needs 60 on a 50-channel link";
    EXPECT_EQ(rm.totalAllocated(), 0u);
    ASSERT_OK(rm.allocate(linkIds({3, 3}), 25));
    EXPECT_EQ(usageOf(rm, 3).allocated, 50u);
    ASSERT_OK(rm.release(linkIds({3, 3}), 25));
    EXPECT_EQ(rm.totalAllocated(), 0u);
}

TEST(ResourceManagerAtomicity, FailedReleaseChangesNothing) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 30));
    ASSERT_OK(rm.allocate(linkIds({2}), 10));
    const auto before = rm.allocations();
    // Link 1 could give back 30, but link 2 holds only 10: nothing may be released.
    ASSERT_ERROR(rm.release(linkIds({1, 2}), 30), ErrorCode::InconsistentState);
    EXPECT_EQ(rm.allocations(), before);
    expectInvariants(rm);
}

// -------------------------------------------------------------------- release

TEST(ResourceManagerRelease, RestoresCapacityExactly) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1, 2}), 40));
    ASSERT_OK(rm.release(linkIds({1, 2}), 40));
    EXPECT_EQ(usageOf(rm, 1).allocated, 0u);
    EXPECT_EQ(usageOf(rm, 1).available, 100u);
    EXPECT_EQ(rm.totalAllocated(), 0u);
    EXPECT_TRUE(rm.allocations().empty()) << "zero entries are dropped";
    expectInvariants(rm);
}

TEST(ResourceManagerRelease, PartialReleaseLeavesTheRest) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 70));
    ASSERT_OK(rm.release(linkIds({1}), 20));
    EXPECT_EQ(usageOf(rm, 1).allocated, 50u);
}

TEST(ResourceManagerRelease, ResourcesAreReusableAfterRelease) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({3}), 50));
    ASSERT_ERROR(rm.allocate(linkIds({3}), 10), ErrorCode::InsufficientCapacity);
    ASSERT_OK(rm.release(linkIds({3}), 50));
    ASSERT_OK(rm.allocate(linkIds({3}), 50));
    EXPECT_EQ(usageOf(rm, 3).allocated, 50u);
    expectInvariants(rm);
}

TEST(ResourceManagerRelease, ReleasingMoreThanAllocatedIsRejected) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 10));
    ASSERT_ERROR(rm.release(linkIds({1}), 11), ErrorCode::InconsistentState);
    EXPECT_EQ(usageOf(rm, 1).allocated, 10u);
}

TEST(ResourceManagerRelease, ReleasingWhatWasNeverAllocatedIsRejected) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_ERROR(rm.release(linkIds({1}), 1), ErrorCode::InconsistentState);
    ASSERT_ERROR(rm.release(linkIds({99}), 1), ErrorCode::InconsistentState);
}

TEST(ResourceManagerRelease, DoubleReleaseIsRejected) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 10));
    ASSERT_OK(rm.release(linkIds({1}), 10));
    ASSERT_ERROR(rm.release(linkIds({1}), 10), ErrorCode::InconsistentState);
}

// ------------------------------------------------------------------ bad input

TEST(ResourceManager, ZeroDemandAndEmptyPathAreInvalid) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_ERROR(rm.allocate(linkIds({1}), 0), ErrorCode::InvalidArgument);
    ASSERT_ERROR(rm.allocate({}, 5), ErrorCode::InvalidArgument);
    ASSERT_ERROR(rm.release(linkIds({1}), 0), ErrorCode::InvalidArgument);
    ASSERT_ERROR(rm.release({}, 5), ErrorCode::InvalidArgument);
    ASSERT_ERROR(rm.canAllocate({}, 5), ErrorCode::InvalidArgument);
    EXPECT_EQ(rm.totalAllocated(), 0u);
}

// ---------------------------------------------------------------- parallel links

TEST(ResourceManager, ParallelLinksHaveIndependentState) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({4}), 60));
    EXPECT_EQ(usageOf(rm, 4).available, 0u);
    EXPECT_EQ(usageOf(rm, 5).available, 60u) << "the other parallel link is unaffected";
    ASSERT_OK(rm.allocate(linkIds({5}), 10));
    expectInvariants(rm);
}

TEST(ResourceManager, HasCapacityReflectsCurrentState) {
    const Topology t = fixture();
    ResourceManager rm{t};
    const FiberLink& l3 = *t.findLink(LinkId{3});
    EXPECT_TRUE(rm.hasCapacity(l3, 50));
    EXPECT_FALSE(rm.hasCapacity(l3, 51));
    ASSERT_OK(rm.allocate(linkIds({3}), 30));
    EXPECT_TRUE(rm.hasCapacity(l3, 20));
    EXPECT_FALSE(rm.hasCapacity(l3, 21));
    EXPECT_FALSE(rm.hasCapacity(l3, 4'000'000'000u)) << "no overflow";
}

TEST(ResourceManager, BidirectionalLinkIsOneSharedPool) {
    // Allocating along 1->2 and along 2->1 draws from the same 100 channels.
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1}), 60));
    ASSERT_ERROR(rm.allocate(linkIds({1}), 60), ErrorCode::InsufficientCapacity);
}

// ----------------------------------------------------------------- consistency

TEST(ResourceManagerConsistency, ValidateIsCleanAfterNormalUse) {
    const Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({1, 2, 3}), 20));
    ASSERT_OK(rm.release(linkIds({1, 2, 3}), 5));
    EXPECT_TRUE(rm.validate().clean());
}

TEST(ResourceManagerConsistency, ValidateDetectsAllocationOnARemovedLink) {
    Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({5}), 30));
    ASSERT_OK(t.removeLink(LinkId{5}));
    const auto report = rm.validate();
    EXPECT_FALSE(report.valid());
    EXPECT_TRUE(report.has(IssueCode::AllocationOnUnknownLink));
    // The stale allocation can still be returned, which heals the state.
    ASSERT_OK(rm.release(linkIds({5}), 30));
    EXPECT_TRUE(rm.validate().clean());
}

TEST(ResourceManagerConsistency, ValidateDetectsAllocationAboveCapacityAfterTheLinkIsReplaced) {
    Topology t = fixture();
    ResourceManager rm{t};
    ASSERT_OK(rm.allocate(linkIds({5}), 50));
    ASSERT_OK(t.removeLink(LinkId{5}));
    ASSERT_OK(t.addLink(routedLink(5, 1, 4, 1, 1, LinkDirection::Bidirectional, 10)));  // same id, smaller capacity
    const auto report = rm.validate();
    EXPECT_FALSE(report.valid());
    EXPECT_TRUE(report.has(IssueCode::AllocationExceedsCapacity));
    const LinkUsage u = usageOf(rm, 5);
    EXPECT_EQ(u.available, 0u) << "available is clamped, never negative";
    EXPECT_EQ(u.allocated, 50u);
}

TEST(ResourceManagerConsistency, SeesLinksAddedToTheTopologyLater) {
    Topology t = fixture();
    const ResourceManager rm{t};
    ASSERT_OK(t.addLink(routedLink(6, 2, 4, 1, 1, LinkDirection::Bidirectional, 25)));
    EXPECT_EQ(usageOf(rm, 6).total, 25u);
    EXPECT_EQ(rm.totalCapacity(), 100u + 100 + 50 + 60 + 60 + 25);
}

// ---------------------------------------------------------------------- stress

// Random allocate/release traffic over 2000 links, checked against an independent shadow ledger.
TEST(ResourceManagerStress, RandomTrafficMatchesAShadowLedger) {
    constexpr std::uint32_t kLinks = 2000;
    constexpr std::uint32_t kNodes = 500;
    std::mt19937 rng(7);
    Topology t;
    for (std::uint32_t i = 1; i <= kNodes; ++i) ASSERT_OK(t.addNode(makeNode(i)));
    std::vector<std::uint32_t> capacity(kLinks + 1);
    for (std::uint32_t l = 1; l <= kLinks; ++l) {
        capacity[l] = 5 + rng() % 40;
        const std::uint32_t a = rng() % kNodes + 1;
        const std::uint32_t b = a % kNodes + 1;
        ASSERT_OK(t.addLink(routedLink(l, a, b, 1, 1, LinkDirection::Bidirectional, capacity[l])));
    }

    ResourceManager rm{t};
    std::vector<std::uint32_t> shadow(kLinks + 1, 0);
    struct Held {
        std::vector<LinkId> links;
        std::uint32_t demand;
    };
    std::vector<Held> held;
    int accepted = 0, rejected = 0, released = 0;

    for (int op = 0; op < 20000; ++op) {
        if (held.empty() || rng() % 3 != 0) {
            Held h;
            h.demand = 1 + rng() % 6;
            const std::size_t length = 1 + rng() % 8;
            std::set<std::uint32_t> chosen;  // distinct links, like a real path
            while (chosen.size() < length) chosen.insert(rng() % kLinks + 1);
            for (auto l : chosen) h.links.push_back(LinkId{l});

            bool fits = true;
            for (auto l : chosen) fits = fits && shadow[l] + h.demand <= capacity[l];
            const auto result = rm.allocate(h.links, h.demand);
            ASSERT_EQ(result.ok(), fits) << "op " << op;
            if (fits) {
                for (auto l : chosen) shadow[l] += h.demand;
                held.push_back(std::move(h));
                ++accepted;
            } else {
                EXPECT_EQ(result.error().code, ErrorCode::InsufficientCapacity);
                ++rejected;
            }
        } else {
            const std::size_t i = rng() % held.size();
            ASSERT_OK(rm.release(held[i].links, held[i].demand));
            for (LinkId l : held[i].links) shadow[l.value()] -= held[i].demand;
            held.erase(held.begin() + static_cast<long>(i));
            ++released;
        }
        if (op % 2000 == 0) expectInvariants(rm);
    }

    for (std::uint32_t l = 1; l <= kLinks; ++l) ASSERT_EQ(rm.allocated(LinkId{l}), shadow[l]) << "link " << l;
    expectInvariants(rm);
    EXPECT_GT(accepted, 3000);
    EXPECT_GT(rejected, 500) << "the test must actually hit capacity limits";
    EXPECT_GT(released, 3000);

    for (const Held& h : held) ASSERT_OK(rm.release(h.links, h.demand));
    EXPECT_EQ(rm.totalAllocated(), 0u);
    EXPECT_TRUE(rm.allocations().empty());
}

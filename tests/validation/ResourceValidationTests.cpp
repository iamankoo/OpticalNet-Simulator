#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

// The error code the ResourceManager should report for a failed allocation, derived independently: bad arguments first,
// then the first problem found scanning links in ascending id order.
ErrorCode expectedAllocationError(const Topology& t, const ShadowLedger& ledger, const std::vector<LinkId>& links, std::uint32_t demand) {
    if (demand == 0 || links.empty()) return ErrorCode::InvalidArgument;
    std::map<LinkId, std::uint64_t> needed;
    for (const LinkId id : links) needed[id] += demand;
    for (const auto& [id, channels] : needed) {
        const FiberLink* link = t.findLink(id);
        if (!link) return ErrorCode::NotFound;
        if (ledger.allocated(id) + channels > link->capacityChannels()) return ErrorCode::InsufficientCapacity;
    }
    return ErrorCode::InvalidArgument;  // not reached for failed allocations
}

struct Held {
    std::vector<LinkId> links;
    std::uint32_t demand;
};

}  // namespace

TEST(ValResources, RandomAllocateReleaseSequencesAgreeWithAnIndependentLedger) {
    int allocOk = 0, allocFail = 0, relOk = 0, relFail = 0;
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig cfg;
        cfg.minNodes = 4;
        cfg.maxNodes = 8;
        cfg.minCapacity = 2;
        cfg.maxCapacity = 6;
        const Topology t = generateRandomTopology(cfg, seed);
        ResourceManager rm{t};
        ShadowLedger ledger(t);
        Rng rng(seed * 31 + 7);
        std::vector<LinkId> all;
        for (const auto& e : t.network().links()) all.push_back(e.first);
        std::vector<Held> held;

        for (int op = 0; op < 300; ++op) {
            SCOPED_TRACE("op " + std::to_string(op));
            if (held.empty() || rng.uniformInt(0, 2) != 0) {
                Held h;
                h.demand = rng.uniformInt(0, 4);  // 0 is invalid on purpose
                const std::uint32_t length = rng.uniformInt(0, 4);  // 0 links is invalid on purpose
                for (std::uint32_t i = 0; i < length; ++i) h.links.push_back(all[rng.uniformIndex(all.size())]);  // repeats allowed
                if (rng.uniformInt(0, 30) == 0) h.links.push_back(LinkId{99999});  // unknown link
                const bool expectedOk = ledger.allocate(h.links, h.demand);
                const auto actual = rm.allocate(h.links, h.demand);
                ASSERT_EQ(actual.ok(), expectedOk);
                if (expectedOk) {
                    held.push_back(h);
                    ++allocOk;
                } else {
                    ++allocFail;
                    EXPECT_EQ(actual.error().code, expectedAllocationError(t, ledger, h.links, h.demand)) << actual.error().describe();
                }
            } else {
                const std::size_t i = rng.uniformIndex(held.size());
                if (rng.uniformInt(0, 9) == 0) {
                    // Try to release one channel more than this connection holds. It must be refused unless other
                    // connections happen to hold the extra channel; either way both models must agree, and a success is
                    // undone so that every connection can still be released exactly later.
                    const bool expectedOk = ledger.release(held[i].links, held[i].demand + 1);
                    const auto actual = rm.release(held[i].links, held[i].demand + 1);
                    ASSERT_EQ(actual.ok(), expectedOk) << (actual.ok() ? "" : actual.error().describe());
                    if (expectedOk) {
                        ASSERT_TRUE(ledger.allocate(held[i].links, held[i].demand + 1));  // put back exactly what was taken
                        ASSERT_OK(rm.allocate(held[i].links, held[i].demand + 1));
                        ++relOk;
                    } else {
                        EXPECT_EQ(actual.error().code, ErrorCode::InconsistentState);
                        ++relFail;
                    }
                } else {
                    const bool expectedOk = ledger.release(held[i].links, held[i].demand);
                    const auto actual = rm.release(held[i].links, held[i].demand);
                    ASSERT_EQ(actual.ok(), expectedOk) << (actual.ok() ? "" : actual.error().describe());
                    ASSERT_TRUE(expectedOk) << "an exact release of what was allocated must succeed";
                    held.erase(held.begin() + static_cast<long>(i));
                    ++relOk;
                }
            }
            for (const LinkId id : all) ASSERT_EQ(rm.allocated(id), ledger.allocated(id)) << "link " << id.value();
            ASSERT_EQ(rm.totalAllocated(), ledger.totalAllocated());
            if (op % 20 == 0) ASSERT_VALID(checkResources(rm, t));
        }
        ASSERT_VALID(checkResources(rm, t));
        for (const Held& h : held) {
            ASSERT_OK(rm.release(h.links, h.demand));
            ASSERT_TRUE(ledger.release(h.links, h.demand));
        }
        EXPECT_EQ(rm.totalAllocated(), 0u) << "releasing everything that was allocated restores the empty state";
        EXPECT_TRUE(rm.allocations().empty());
    }
    EXPECT_GT(allocOk, 3000);
    EXPECT_GT(allocFail, 1000);
    EXPECT_GT(relOk, 1000);
    EXPECT_GT(relFail, 30);
}

TEST(ValResources, FailedOperationsLeaveTheStateExactlyAsItWas) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        ResourceManager rm{t};
        Rng rng(seed);
        std::vector<LinkId> all;
        for (const auto& e : t.network().links()) all.push_back(e.first);
        for (int i = 0; i < 40; ++i) (void)rm.allocate({&all[rng.uniformIndex(all.size())], 1}, rng.uniformInt(1, 3));
        const auto snapshot = rm.allocations();
        // A path whose last link is certainly impossible (unknown) must not leave the earlier links half-allocated.
        std::vector<LinkId> poisoned = {all.front(), LinkId{88888}};
        ASSERT_FALSE(rm.allocate(poisoned, 1).ok());
        // A demand no link can ever hold.
        ASSERT_FALSE(rm.allocate({&all.front(), 1}, 1000).ok());
        EXPECT_EQ(rm.allocations(), snapshot);
        ASSERT_VALID(checkResources(rm, t));
    }
}

TEST(ValResources, AggregateAccountingMatchesPerLinkSums) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 3);
    ResourceManager rm{t};
    std::uint64_t capacity = 0;
    for (const auto& e : t.network().links()) capacity += e.second.capacityChannels();
    EXPECT_EQ(rm.totalCapacity(), capacity);
    EXPECT_EQ(rm.totalAvailable(), capacity);
    std::uint64_t expectedAllocated = 0;
    for (const auto& e : t.network().links()) {
        const LinkId id = e.first;
        const std::uint32_t take = e.second.capacityChannels() / 2;
        if (take == 0) continue;
        ASSERT_OK(rm.allocate({&id, 1}, take));
        expectedAllocated += take;
        EXPECT_EQ(rm.totalAllocated(), expectedAllocated);
        EXPECT_EQ(rm.totalAvailable(), capacity - expectedAllocated);
    }
    ASSERT_VALID(checkResources(rm, t));
}

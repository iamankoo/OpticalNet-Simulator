#include <gtest/gtest.h>

#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

#include "TestHelpers.hpp"
#include "opticalnet/simulation/Rng.hpp"
#include "opticalnet/simulation/SimulationConfig.hpp"
#include "opticalnet/simulation/SimulationEvent.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

// ------------------------------------------------------------------------- Rng

TEST(Rng, SameSeedGivesTheSameSequence) {
    Rng a(12345), b(12345);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(a.exponential(3.0), b.exponential(3.0));
        EXPECT_EQ(a.uniformInt(1, 1000), b.uniformInt(1, 1000));
        EXPECT_EQ(a.uniformIndex(17), b.uniformIndex(17));
    }
}

TEST(Rng, DifferentSeedsGiveDifferentSequences) {
    Rng a(1), b(2);
    std::vector<double> x, y;
    for (int i = 0; i < 50; ++i) {
        x.push_back(a.exponential(1.0));
        y.push_back(b.exponential(1.0));
    }
    EXPECT_NE(x, y);
}

TEST(Rng, ValuesStayInTheirRanges) {
    Rng rng(7);
    for (int i = 0; i < 5000; ++i) {
        EXPECT_GE(rng.exponential(2.0), 0.0);
        const auto v = rng.uniformInt(3, 9);
        EXPECT_GE(v, 3u);
        EXPECT_LE(v, 9u);
        EXPECT_LT(rng.uniformIndex(5), 5u);
    }
    EXPECT_EQ(rng.uniformInt(4, 4), 4u) << "a one-value range is allowed";
    EXPECT_EQ(rng.uniformIndex(1), 0u);
}

TEST(Rng, UniformIntCoversTheWholeRange) {
    Rng rng(99);
    std::set<std::uint32_t> seen;
    for (int i = 0; i < 2000; ++i) seen.insert(rng.uniformInt(1, 6));
    EXPECT_EQ(seen.size(), 6u);
}

TEST(Rng, ExponentialSampleMeanIsCloseToTheParameter) {
    Rng rng(2024);
    double sum = 0.0;
    constexpr int kSamples = 50000;
    for (int i = 0; i < kSamples; ++i) sum += rng.exponential(4.0);
    EXPECT_NEAR(sum / kSamples, 4.0, 0.2);
}

TEST(Rng, PreconditionViolationsAreProgrammingErrors) {
    Rng rng(1);
    EXPECT_THROW((void)rng.exponential(0.0), std::invalid_argument);
    EXPECT_THROW((void)rng.exponential(-1.0), std::invalid_argument);
    EXPECT_THROW((void)rng.exponential(std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformInt(5, 4), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformIndex(0), std::invalid_argument);
}

// ------------------------------------------------------------------ EventQueue

namespace {

SimulationEvent drainOne(EventQueue& q) { return q.pop(); }

std::vector<std::uint32_t> drainIds(EventQueue& q) {
    std::vector<std::uint32_t> ids;
    while (!q.empty()) ids.push_back(drainOne(q).connection.value());
    return ids;
}

}  // namespace

TEST(EventQueue, StartsEmpty) {
    EventQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_THROW((void)q.peek(), std::logic_error);
    EXPECT_THROW((void)q.pop(), std::logic_error);
}

TEST(EventQueue, EventsComeOutInChronologicalOrder) {
    EventQueue q;
    for (const auto& [t, id] : std::vector<std::pair<double, std::uint32_t>>{{5.0, 1}, {1.0, 2}, {12.0, 3}, {3.5, 4}, {0.0, 5}})
        ASSERT_OK(q.schedule(t, EventType::ConnectionArrival, ConnectionId{id}));
    EXPECT_EQ(q.size(), 5u);
    EXPECT_EQ(drainIds(q), (std::vector<std::uint32_t>{5, 2, 4, 1, 3}));
    EXPECT_TRUE(q.empty());
}

TEST(EventQueue, SameTimestampReleasesComeBeforeArrivals) {
    EventQueue q;
    ASSERT_OK(q.schedule(4.0, EventType::ConnectionArrival, ConnectionId{1}));
    ASSERT_OK(q.schedule(4.0, EventType::ConnectionRelease, ConnectionId{2}));
    ASSERT_OK(q.schedule(4.0, EventType::ConnectionArrival, ConnectionId{3}));
    ASSERT_OK(q.schedule(4.0, EventType::ConnectionRelease, ConnectionId{4}));
    EXPECT_EQ(drainIds(q), (std::vector<std::uint32_t>{2, 4, 1, 3})) << "releases first, then arrivals, each in scheduling order";
}

TEST(EventQueue, SameTimeAndTypeFollowSchedulingOrder) {
    EventQueue q;
    for (std::uint32_t id : {9u, 3u, 7u, 1u}) ASSERT_OK(q.schedule(2.0, EventType::ConnectionArrival, ConnectionId{id}));
    EXPECT_EQ(drainIds(q), (std::vector<std::uint32_t>{9, 3, 7, 1})) << "ties are broken by sequence, not by connection id";
}

TEST(EventQueue, SequenceNumbersAreAssignedInSchedulingOrder) {
    EventQueue q;
    const auto first = must(q.schedule(5.0, EventType::ConnectionArrival, ConnectionId{1}));
    const auto second = must(q.schedule(1.0, EventType::ConnectionRelease, ConnectionId{2}));
    EXPECT_EQ(first, 0u);
    EXPECT_EQ(second, 1u);
    EXPECT_EQ(q.peek().sequence, 1u);
    EXPECT_EQ(q.pop().sequence, 1u);
    EXPECT_EQ(q.pop().sequence, 0u);
}

TEST(EventQueue, TimeOrderBeatsTypeOrder) {
    EventQueue q;
    ASSERT_OK(q.schedule(5.0, EventType::ConnectionRelease, ConnectionId{1}));
    ASSERT_OK(q.schedule(4.0, EventType::ConnectionArrival, ConnectionId{2}));
    EXPECT_EQ(drainIds(q), (std::vector<std::uint32_t>{2, 1})) << "an earlier arrival precedes a later release";
}

TEST(EventQueue, EventBeyondAnyHorizonIsSimplyStoredLast) {
    EventQueue q;
    ASSERT_OK(q.schedule(1e12, EventType::ConnectionRelease, ConnectionId{1}));
    ASSERT_OK(q.schedule(1.0, EventType::ConnectionArrival, ConnectionId{2}));
    EXPECT_EQ(q.peek().connection, ConnectionId{2});
    (void)q.pop();
    EXPECT_DOUBLE_EQ(q.peek().time, 1e12);
}

TEST(EventQueue, InvalidTimesAreRejected) {
    EventQueue q;
    ASSERT_ERROR(q.schedule(-0.5, EventType::ConnectionArrival, ConnectionId{1}), ErrorCode::InvalidArgument);
    ASSERT_ERROR(q.schedule(std::numeric_limits<double>::quiet_NaN(), EventType::ConnectionArrival, ConnectionId{1}),
                 ErrorCode::InvalidArgument);
    ASSERT_ERROR(q.schedule(std::numeric_limits<double>::infinity(), EventType::ConnectionArrival, ConnectionId{1}),
                 ErrorCode::InvalidArgument);
    EXPECT_TRUE(q.empty());
    ASSERT_OK(q.schedule(0.0, EventType::ConnectionArrival, ConnectionId{1})) << "time zero is valid";
}

// ------------------------------------------------------------ SimulationConfig

TEST(SimulationConfig, DefaultsAreValid) {
    EXPECT_TRUE(SimulationConfig{}.validate().ok());
}

TEST(SimulationConfig, RejectsInvalidParameters) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const auto invalid = [](auto mutate) {
        SimulationConfig c;
        mutate(c);
        const auto r = c.validate();
        return !r.ok() && r.error().code == ErrorCode::InvalidArgument;
    };
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.endTime = -1; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.endTime = nan; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.endTime = inf; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.arrivalRate = -0.1; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.arrivalRate = inf; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.meanLifetime = 0; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.meanLifetime = -3; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.minCapacityChannels = 0; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.minCapacityChannels = 5; c.maxCapacityChannels = 4; }));
    EXPECT_TRUE(invalid([&](SimulationConfig& c) { c.maxRequests = 5'000'000'000ULL; }));
}

TEST(SimulationConfig, BoundaryValuesAreAccepted) {
    SimulationConfig c;
    c.endTime = 0;
    c.arrivalRate = 0;
    c.maxRequests = 0;
    c.minCapacityChannels = 3;
    c.maxCapacityChannels = 3;
    EXPECT_TRUE(c.validate().ok());
}

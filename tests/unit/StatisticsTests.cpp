#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "opticalnet/simulation/Statistics.hpp"

using namespace opticalnet;

TEST(Statistics, EmptyInputGivesZeros) {
    const SampleStatistics s = summarize({});
    EXPECT_EQ(s.count, 0u);
    EXPECT_EQ(s.mean, 0.0);
    EXPECT_EQ(s.stdDev, 0.0);
    EXPECT_EQ(s.confidenceHalfWidth95, 0.0);
}

TEST(Statistics, SingleObservationHasNoSpreadEstimate) {
    const std::vector<double> v{4.5};
    const SampleStatistics s = summarize(v);
    EXPECT_EQ(s.count, 1u);
    EXPECT_DOUBLE_EQ(s.mean, 4.5);
    EXPECT_EQ(s.stdDev, 0.0);
    EXPECT_EQ(s.standardError, 0.0);
    EXPECT_EQ(s.confidenceHalfWidth95, 0.0);
}

TEST(Statistics, KnownSample) {
    const std::vector<double> v{1, 2, 3, 4, 5};
    const SampleStatistics s = summarize(v);
    EXPECT_EQ(s.count, 5u);
    EXPECT_DOUBLE_EQ(s.mean, 3.0);
    EXPECT_NEAR(s.stdDev, std::sqrt(2.5), 1e-12) << "sample variance divides by n - 1";
    EXPECT_NEAR(s.standardError, std::sqrt(2.5) / std::sqrt(5.0), 1e-12);
    EXPECT_NEAR(s.confidenceHalfWidth95, 2.776 * s.standardError, 1e-9) << "t(0.975, 4 df) = 2.776";
}

TEST(Statistics, IdenticalValuesHaveZeroSpread) {
    const std::vector<double> v(10, 0.25);
    const SampleStatistics s = summarize(v);
    EXPECT_DOUBLE_EQ(s.mean, 0.25);
    EXPECT_EQ(s.stdDev, 0.0);
    EXPECT_EQ(s.confidenceHalfWidth95, 0.0);
}

TEST(Statistics, TwoObservations) {
    const std::vector<double> v{10, 20};
    const SampleStatistics s = summarize(v);
    EXPECT_DOUBLE_EQ(s.mean, 15.0);
    EXPECT_NEAR(s.stdDev, std::sqrt(50.0), 1e-12);
    EXPECT_NEAR(s.confidenceHalfWidth95, 12.706 * s.standardError, 1e-9);
}

TEST(Statistics, StudentTTableValues) {
    EXPECT_DOUBLE_EQ(studentT95(1), 12.706);
    EXPECT_DOUBLE_EQ(studentT95(4), 2.776);
    EXPECT_DOUBLE_EQ(studentT95(10), 2.228);
    EXPECT_DOUBLE_EQ(studentT95(30), 2.042);
}

TEST(Statistics, StudentTApproximationAboveTheTable) {
    EXPECT_NEAR(studentT95(40), 2.021, 0.002);
    EXPECT_NEAR(studentT95(60), 2.000, 0.002);
    EXPECT_NEAR(studentT95(120), 1.980, 0.002);
    EXPECT_NEAR(studentT95(100000), 1.96, 0.001);
    EXPECT_GT(studentT95(31), studentT95(1000)) << "decreasing towards 1.96";
}

TEST(Statistics, StudentTRejectsZeroDegreesOfFreedom) {
    EXPECT_THROW((void)studentT95(0), std::invalid_argument);
}

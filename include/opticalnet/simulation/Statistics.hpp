#pragma once

#include <cstddef>
#include <span>

namespace opticalnet {

// Summary of independent observations of one quantity (one value per replication).
//
//   mean                 = sum / n
//   stdDev               = sample standard deviation (divides by n - 1); 0 if n < 2
//   standardError        = stdDev / sqrt(n); 0 if n < 2
//   confidenceHalfWidth95 = t(0.975, n - 1) * standardError; 0 if n < 2
//
// The 95% interval mean +/- confidenceHalfWidth95 assumes the observations are independent (true for
// replications, which use independent seeds) and roughly normally distributed (reasonable for
// per-replication averages, but not guaranteed, and each replication includes the start-up transient from an
// empty network). With n < 2 there is no spread estimate, so the half-width is reported as 0, which means
// "unknown", not "exact".
struct SampleStatistics {
    std::size_t count = 0;
    double mean = 0.0;
    double stdDev = 0.0;
    double standardError = 0.0;
    double confidenceHalfWidth95 = 0.0;

    friend bool operator==(const SampleStatistics&, const SampleStatistics&) = default;
};

[[nodiscard]] SampleStatistics summarize(std::span<const double> values);

// Two-sided 95% critical value of Student's t distribution (exact table for 1..30 degrees of freedom, the
// first-order Cornish-Fisher approximation 1.96 + 2.373 / df above that, accurate to about 0.1%).
// Precondition: degreesOfFreedom >= 1.
[[nodiscard]] double studentT95(std::size_t degreesOfFreedom);

}  // namespace opticalnet

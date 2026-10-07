#include "opticalnet/simulation/Statistics.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace opticalnet {

double studentT95(std::size_t df) {
    if (df == 0) throw std::invalid_argument("studentT95: degrees of freedom must be at least 1");
    static constexpr std::array<double, 30> table = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
        2.201,  2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
        2.080,  2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
    if (df <= table.size()) return table[df - 1];
    return 1.96 + 2.373 / static_cast<double>(df);
}

SampleStatistics summarize(std::span<const double> values) {
    SampleStatistics s;
    s.count = values.size();
    if (values.empty()) return s;
    double sum = 0.0;
    for (const double v : values) sum += v;
    s.mean = sum / static_cast<double>(values.size());
    if (values.size() < 2) return s;
    double squares = 0.0;
    for (const double v : values) squares += (v - s.mean) * (v - s.mean);
    s.stdDev = std::sqrt(squares / static_cast<double>(values.size() - 1));
    s.standardError = s.stdDev / std::sqrt(static_cast<double>(values.size()));
    s.confidenceHalfWidth95 = studentT95(values.size() - 1) * s.standardError;
    return s;
}

}  // namespace opticalnet

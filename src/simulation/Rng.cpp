#include "opticalnet/simulation/Rng.hpp"

#include <cmath>
#include <stdexcept>

namespace opticalnet {

double Rng::exponential(double mean) {
    if (!(mean > 0.0) || !std::isfinite(mean)) throw std::invalid_argument("Rng::exponential: mean must be positive and finite");
    return std::exponential_distribution<double>(1.0 / mean)(engine_);
}

std::uint32_t Rng::uniformInt(std::uint32_t lo, std::uint32_t hi) {
    if (lo > hi) throw std::invalid_argument("Rng::uniformInt: lo must not exceed hi");
    return std::uniform_int_distribution<std::uint32_t>(lo, hi)(engine_);
}

std::size_t Rng::uniformIndex(std::size_t count) {
    if (count == 0) throw std::invalid_argument("Rng::uniformIndex: count must be positive");
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(engine_);
}

}  // namespace opticalnet

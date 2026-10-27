#pragma once

#include <cstddef>
#include <cstdint>
#include <random>

namespace opticalnet {

// splitmix64 finalizer: a bijective 64-bit mixer, used to derive independent RNG stream seeds.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Seeded random source for simulations: std::mt19937_64 plus standard-library distributions.
//
// The same seed gives the same sequence for a given compiler/standard library. The C++ standard
// fixes the engine's output but not the distributions' algorithms, so the exact numbers can differ
// between standard-library implementations; tests therefore check reproducibility and ranges, not
// hard-coded random values.
//
// Preconditions (mean > 0, lo <= hi, count > 0) are programming errors and throw
// std::invalid_argument; callers validate configuration before drawing.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}

    // Exponential distribution with the given mean (rate = 1 / mean). Result >= 0.
    [[nodiscard]] double exponential(double mean);
    // Uniform integer in the closed range [lo, hi].
    [[nodiscard]] std::uint32_t uniformInt(std::uint32_t lo, std::uint32_t hi);
    // Uniform index in [0, count).
    [[nodiscard]] std::size_t uniformIndex(std::size_t count);

private:
    std::mt19937_64 engine_;
};

}  // namespace opticalnet

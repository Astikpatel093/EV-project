// rng.hpp - deterministic random numbers for data generation, tests and benchmarks.
//
// std::mt19937 is portable, but std::uniform_int_distribution and friends are NOT: GCC, Clang and
// MSVC may turn the same engine output into different numbers. To make every generated file
// byte-identical on every compiler, EV33 uses its own xorshift64* generator and its own
// range/unit conversions. Never seed from the clock where reproducibility is required.
#pragma once

#include <cstdint>

namespace ev {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) { reseed(seed); }

    void reseed(std::uint64_t seed) {
        // splitmix64 scrambles the seed so that nearby seeds give unrelated streams
        std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        state_ = (z ^ (z >> 31)) | 1ULL;        // xorshift state must be non-zero
    }

    std::uint64_t next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

    // uniform double in [0, 1) using the top 53 bits
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

    // uniform integer in [lo, hi] (inclusive); modulo bias is below 2^-40 for ranges < 2^24
    std::int64_t range(std::int64_t lo, std::int64_t hi) {
        if (hi <= lo) return lo;
        const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1ULL;
        return lo + static_cast<std::int64_t>(next() % span);
    }

    double uniform(double lo, double hi) { return lo + (hi - lo) * unit(); }

private:
    std::uint64_t state_ = 1;
};

} // namespace ev

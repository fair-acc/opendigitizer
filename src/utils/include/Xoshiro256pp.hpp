#ifndef OPENDIGITIZER_XOSHIRO256PP_HPP
#define OPENDIGITIZER_XOSHIRO256PP_HPP

#include <array>
#include <concepts>
#include <cstdint>

namespace opendigitizer {

/**
 * @brief Fast non-crypto PRNG for DSP/simulation; provides uniform and cheap “semi-uniform” (triangular) noise.
 *
 * @details Engine: xoshiro256++ (small state, high throughput); seed via SplitMix64 (avoid zero state).
 * Triangular noise = (u1 + u2 - 1): Irwin–Hall(n=2), i.e. triangular distribution.
 * @see D. Blackman, S. Vigna, “Scrambled Linear Pseudorandom Number Generators”, arXiv:1805.01407.
 * @see xoshiro256++ + SplitMix64 reference code: https://prng.di.unimi.it/
 * @see Irwin–Hall distribution (n=2): https://en.wikipedia.org/wiki/Irwin%E2%80%93Hall_distribution
 */
struct Xoshiro256pp {
    std::array<std::uint64_t, 4> _s{};

    static constexpr std::uint64_t splitmix64(std::uint64_t& x) {
        x += 0x9E3779B97F4A7C15ULL;
        auto z = x;
        z      = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z      = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    constexpr explicit Xoshiro256pp(std::uint64_t seed = 1) {
        auto x = seed;
        for (auto& v : _s) {
            v = splitmix64(x);
        }
    }

    static constexpr std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    constexpr std::uint64_t nextU64() {
        const auto result = rotl(_s[0] + _s[3], 23) + _s[0];
        const auto t      = _s[1] << 17;
        _s[2] ^= _s[0];
        _s[3] ^= _s[1];
        _s[1] ^= _s[2];
        _s[0] ^= _s[3];
        _s[2] ^= t;
        _s[3] = rotl(_s[3], 45);
        return result;
    }

    template<std::floating_point T = double>
    constexpr T uniform01() { // [0,1)
        if constexpr (std::same_as<T, float>) {
            return static_cast<float>(nextU64() >> 40) * (1.0f / 16777216.0f); // 24-bit
        } else {
            return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0); // 53-bit
        }
    }

    template<std::floating_point T = double>
    constexpr T uniformM11() {
        return T(2) * uniform01<T>() - T(1);
    } // [-1,1)

    template<std::floating_point T = double>
    constexpr T triangularM11() {
        return uniform01<T>() + uniform01<T>() - T(1);
    } // triangular [-1,1)
};

} // namespace opendigitizer

#endif // OPENDIGITIZER_XOSHIRO256PP_HPP

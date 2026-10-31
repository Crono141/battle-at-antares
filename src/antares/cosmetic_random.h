#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>

// The cosmetic random generator (D16, WP0.7). Drawing, sound and flavour-text choices draw from
// here, so the rules generator (game_random) only ever feeds the rules: what the player looks at,
// and which frontend draws it, can't change an outcome. The original already brackets most of its
// cosmetic draws with a seed save and restore; the call sites that use this one were found
// unbracketed by tools/antares-test/rng_audit.py (notes/antares/rng.md).
//
// Seeded from the clock on first use: cosmetic choices needn't repeat between runs. Same contract
// as game_random::Random: returns 1..max_val, and 1 for a zero maximum. Header-only so unit-test
// targets need no build changes. Game thread only.
namespace antares::cosmetic_random {
    inline uint64_t g_state = 0;

    inline void Seed(uint64_t seed) {
        g_state = seed != 0 ? seed : 0x9E3779B97F4A7C15ull;
    }

    inline uint32_t Random(uint32_t max_val) {
        if (max_val == 0) {
            return 1;
        }
        if (g_state == 0) {
            const auto now = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
            Seed(now ^ ((uint64_t)std::time(nullptr) << 32));
        }
        // splitmix64; the modulo bias is irrelevant for the small ranges used here.
        g_state += 0x9E3779B97F4A7C15ull;
        uint64_t z = g_state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (uint32_t)(z % max_val) + 1;
    }
} // namespace antares::cosmetic_random

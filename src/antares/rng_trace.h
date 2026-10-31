#pragma once

#include <cstdint>
#include <unordered_map>

// Counts calls to the shared random generator (game_random::Random), so the rules replay probe
// (WP0.5) can see how many numbers each turn drew and which functions drew them. Header-only
// because random.cpp is linked into many unit-test targets. Game thread only.
namespace antares::rng_trace {
    inline uint64_t g_calls = 0;
    // Per-caller counts, only kept while the rules trace is on (ANTARES_RULES_TRACE).
    inline bool g_by_caller = false;
    inline std::unordered_map<const void*, uint64_t> g_callers;

    inline void Note_Call(const void* caller) {
        ++g_calls;
        if (g_by_caller) {
            ++g_callers[caller];
        }
    }
} // namespace antares::rng_trace

#if defined(_MSC_VER)
#include <intrin.h>
#define ANTARES_RNG_CALLER() _ReturnAddress()
#else
#define ANTARES_RNG_CALLER() __builtin_return_address(0)
#endif

#pragma once

// Rules replay probe (WP0.5). With ANTARES_RULES_TRACE=1 the game logs one line per mark: the
// stardate, the shared generator's seed, how many random numbers were drawn since the previous
// mark, and a hash of the game as it would be saved. Below each line it lists the functions that
// drew them (module offsets; tools/antares-test/replay.py names them with addr2line). Comparing
// two runs of the same input script shows whether the rules are deterministic and, if not, where
// drawing code consumed numbers the rules would otherwise have got.
namespace antares::rules_trace {
    /** Logs a mark named `where` (for example "load", "turn-start", "turn-end"). No-op unless
     *  ANTARES_RULES_TRACE is set. Game thread only. */
    void Mark(const char* where);
} // namespace antares::rules_trace

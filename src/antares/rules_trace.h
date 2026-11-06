#pragma once

// Rules replay probe (WP0.5). With ANTARES_RULES_TRACE=1 the game logs one line per mark: the
// stardate, the shared generator's seed, how many random numbers were drawn since the previous
// mark, and a hash of the game as it would be saved. Below each line it lists the functions that
// drew them (module offsets; tools/antares-test/replay.py names them with addr2line). Comparing
// two runs of the same input script shows whether the rules are deterministic and, if not, where
// drawing code consumed numbers the rules would otherwise have got.
#include <cstdint>
#include <string>

namespace antares::rules_trace {
    /** Logs a mark named `where` (for example "load", "turn-start", "turn-end"). No-op unless
     *  ANTARES_RULES_TRACE is set. Game thread only. */
    void Mark(const char* where);

    /** FNV-1a over the save the game would write now, with the seed zeroed and a fixed
     *  description; 0 with `error` set when the save can't be written. Works whether or not the
     *  trace is on (the read-only query guard uses it, WP3.2b). Game thread only. */
    uint64_t State_Hash(std::string* error);
} // namespace antares::rules_trace

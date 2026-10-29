#pragma once

#include <string>

#include "yyjson.h"

// Antares additions to the package manifest (mod.json5). `requires.engine` keeps checking the mod
// API version (Redone's 2.x), so Redone mods load unchanged. Antares-only packages may add
// `requires.antares`, a version range checked against antares::PRODUCT_VERSION.
namespace antares {
    /** Validates an optional `requires.antares` value (nullptr when absent). Stores the range and
     *  returns true when it is absent or matches; otherwise sets `error`. */
    bool Check_Requires_Antares(yyjson_val* value, std::string* range, std::string* error);
} // namespace antares

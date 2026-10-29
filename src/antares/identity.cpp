#include "pch.h"

#include "antares/identity.h"

#include <cstdlib>

namespace antares {
    std::string Resolve_Startup_Config_Path(const std::string& requested, bool selected, const std::function<bool(const char*)>& file_exists) {
        if (selected || file_exists(CONFIG_FILE) || !file_exists(LEGACY_CONFIG_FILE)) {
            return requested;
        }
        return LEGACY_CONFIG_FILE;
    }

    const char* Get_Env(const char* name) {
        const std::string suffix(name);
        if (const char* value = std::getenv(("ANTARES_" + suffix).c_str())) {
            return value;
        }
        return std::getenv(("ORION2RE_" + suffix).c_str());
    }
} // namespace antares

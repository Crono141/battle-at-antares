#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Battle at Antares product identity. The product version is what players see (title, README,
// releases). It is separate from orion2re::ENGINE_VERSION in version.h, which is the mod API
// version that packages check with `requires.engine`; that stays at Redone's 2.x so Redone mods
// keep loading. Antares-only packages add `requires.antares`, checked against PRODUCT_VERSION.
namespace antares {
    inline constexpr char PRODUCT_NAME[] = "Battle at Antares";
    inline constexpr uint32_t PRODUCT_VERSION_MAJOR = 0;
    inline constexpr uint32_t PRODUCT_VERSION_MINOR = 1;
    inline constexpr uint32_t PRODUCT_VERSION_PATCH = 0;
    inline constexpr char PRODUCT_VERSION[] = "0.1.0";
    /** Shown on the main menu where the original prints "Version 2.00". */
    inline constexpr char PRODUCT_VERSION_LABEL[] = "Antares 0.1.0";

    inline constexpr char CONFIG_FILE[] = "antares.cfg";
    /** Read on first run when antares.cfg doesn't exist yet. */
    inline constexpr char LEGACY_CONFIG_FILE[] = "orion2re.cfg";

    /** Picks the startup config: an explicit -config path as given, otherwise antares.cfg, or
     *  orion2re.cfg when only that one exists. */
    std::string Resolve_Startup_Config_Path(const std::string& requested, bool selected, const std::function<bool(const char*)>& file_exists);

    /** Reads ANTARES_<name>, falling back to the legacy ORION2RE_<name> for one release. */
    const char* Get_Env(const char* name);
} // namespace antares

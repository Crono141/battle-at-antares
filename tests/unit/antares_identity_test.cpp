#include "pch.h"

#include "antares/identity.h"
#include "doctest/doctest.h"

#include <cstdlib>
#include <set>
#include <string>

namespace {
    std::function<bool(const char*)> Files_(std::set<std::string> present) {
        return [present](const char* path) { return present.count(path) != 0; };
    }
} // namespace

DOCTEST_TEST_CASE("startup config prefers antares.cfg and reads orion2re.cfg on first run") {
    using antares::Resolve_Startup_Config_Path;
    DOCTEST_CHECK(Resolve_Startup_Config_Path("antares.cfg", false, Files_({"antares.cfg", "orion2re.cfg"})) == "antares.cfg");
    DOCTEST_CHECK(Resolve_Startup_Config_Path("antares.cfg", false, Files_({"orion2re.cfg"})) == "orion2re.cfg");
    DOCTEST_CHECK(Resolve_Startup_Config_Path("antares.cfg", false, Files_({})) == "antares.cfg");
    // An explicit -config is never redirected.
    DOCTEST_CHECK(Resolve_Startup_Config_Path("profiles/a.cfg", true, Files_({"orion2re.cfg"})) == "profiles/a.cfg");
}

DOCTEST_TEST_CASE("environment knobs accept the legacy ORION2RE_ prefix") {
#if defined(_WIN32)
    _putenv_s("ANTARES_IDENTITY_TEST", "");
    _putenv_s("ORION2RE_IDENTITY_TEST", "old");
#else
    unsetenv("ANTARES_IDENTITY_TEST");
    setenv("ORION2RE_IDENTITY_TEST", "old", 1);
#endif
    DOCTEST_REQUIRE(antares::Get_Env("IDENTITY_TEST") != nullptr);
    DOCTEST_CHECK(std::string(antares::Get_Env("IDENTITY_TEST")) == "old");
#if defined(_WIN32)
    _putenv_s("ANTARES_IDENTITY_TEST", "new");
#else
    setenv("ANTARES_IDENTITY_TEST", "new", 1);
#endif
    DOCTEST_CHECK(std::string(antares::Get_Env("IDENTITY_TEST")) == "new");
}

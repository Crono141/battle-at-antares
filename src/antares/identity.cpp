#include "pch.h"

#include "antares/identity.h"

#include "antares/host.h"
#include "startup_directory.h"

#include <filesystem>

#include <cstdlib>

namespace antares {
    bool Set_Game_Directory(std::string* directory, std::string* error) {
        if (!host::Active()) {
            return startup::Set_Game_Directory(directory, error);
        }
        std::error_code cwd_error;
        const std::filesystem::path cwd = std::filesystem::current_path(cwd_error);
        if (cwd_error) {
            *error = "unable to read the host's game folder: " + cwd_error.message();
            return false;
        }
        const std::u8string text = (cwd / "").u8string();
        directory->assign(text.begin(), text.end());
        error->clear();
        return true;
    }

    std::string Install_Directory() {
        if (host::Active()) {
            std::error_code error;
            const std::u8string text = (std::filesystem::current_path(error) / "").u8string();
            if (!error) return std::string(text.begin(), text.end());
        }
        const char* base = SDL_GetBasePath();
        return base != nullptr ? base : "";
    }

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

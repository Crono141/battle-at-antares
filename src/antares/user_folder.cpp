#include "pch.h"

#include "antares/user_folder.h"

#include "game/user_paths.h"

#include <filesystem>
#include <fstream>

#if !defined(_WIN32)
#include <pwd.h>
#include <unistd.h>
#endif

namespace antares::user_folder {
    namespace {
        constexpr const char* DEFAULT_DATA_DIRS = "/usr/local/share/:/usr/share/";

        std::string With_Separator_(std::string path) {
            if (!path.empty() && path.back() != '/' && path.back() != '\\') {
                path += '/';
            }
            return path;
        }

        // Absolute for any platform's rules, so Windows and macOS layouts can be tested on Linux:
        // "/x", "C:\x" or "C:/x", "\\server\x".
        bool Is_Absolute_(const char* path) {
            if (path == nullptr || path[0] == '\0') return false;
            if (path[0] == '/' || (path[0] == '\\' && path[1] == '\\')) return true;
            return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
                (path[2] == '\\' || path[2] == '/');
        }

        const char* Absolute_Variable_(const char* name) {
            const char* value = SDL_getenv(name);
            return Is_Absolute_(value) ? value : nullptr;
        }

        bool Home_(std::string* home) {
            if (const char* value = Absolute_Variable_("HOME")) {
                *home = With_Separator_(value);
                return true;
            }
#if !defined(_WIN32)
            const struct passwd* entry = getpwuid(getuid());
            if (entry != nullptr && Is_Absolute_(entry->pw_dir)) {
                *home = With_Separator_(entry->pw_dir);
                return true;
            }
#endif
            return false;
        }

        std::string Xdg_Base_(const char* variable, const std::string& home, const char* home_relative) {
            const char* value = Absolute_Variable_(variable);
            return value != nullptr ? With_Separator_(value) : home + home_relative;
        }

        // The first set, non-empty override wins: "0" asks for portable, anything else for the
        // user folder.
        const char* Override_() {
            for (const char* name : {"ANTARES_USER_FOLDER", "ANTARES_XDG", "ORION2RE_XDG"}) {
                const char* value = SDL_getenv(name);
                if (value != nullptr && value[0] != '\0') return value;
            }
            return nullptr;
        }

        bool Writable_(const std::string& directory) {
#if defined(_WIN32)
            // access() says nothing useful about Program Files; try a file.
            const std::filesystem::path probe = std::filesystem::u8path(directory) / ".antares-write-test";
            {
                std::ofstream file(probe, std::ios::binary);
                if (!file.good()) return false;
            }
            std::error_code error;
            std::filesystem::remove(probe, error);
            return true;
#else
            return access(directory.c_str(), W_OK) == 0;
#endif
        }

        bool Exists_(const std::string& path) {
            SDL_PathInfo info;
            return SDL_GetPathInfo(path.c_str(), &info);
        }
    } // namespace

    bool Selected(const std::string& install_directory) {
        if (const char* value = Override_()) {
            return strcmp(value, "0") != 0;
        }
        const std::string directory = With_Separator_(install_directory);
        if (Exists_(directory + MARKER_NAME) || Exists_(directory + user_paths::XDG_MARKER_NAME)) {
            return true;
        }
        return !Writable_(directory);
    }

    bool Resolve(const std::string& install_directory, e_platform platform, s_layout* layout) {
        if (platform == PLATFORM_PORTABLE_ONLY || install_directory.empty() || !Selected(install_directory)) {
            return false;
        }
        *layout = {};
        if (platform == PLATFORM_LINUX) {
            std::string home;
            if (!Home_(&home)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "user folder: no home folder; using portable mode");
                return false;
            }
            const std::string name = std::string(XDG_NAME) + "/";
            const std::string data_home = Xdg_Base_("XDG_DATA_HOME", home, ".local/share/");
            layout->config = Xdg_Base_("XDG_CONFIG_HOME", home, ".config/") + name;
            layout->data = data_home + name;
            layout->state = Xdg_Base_("XDG_STATE_HOME", home, ".local/state/") + name;
            layout->cache = Xdg_Base_("XDG_CACHE_HOME", home, ".cache/") + name;
            layout->game_asset_directories.push_back(layout->data + "game/");
            layout->game_asset_directories.push_back(layout->data);
            // System-wide copies (a distribution package of the original files).
            const char* data_dirs = SDL_getenv("XDG_DATA_DIRS");
            const std::string dirs = data_dirs != nullptr && data_dirs[0] != '\0' ? data_dirs : DEFAULT_DATA_DIRS;
            size_t start = 0;
            while (start <= dirs.size()) {
                const size_t end = dirs.find(':', start);
                const std::string entry = dirs.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (Is_Absolute_(entry.c_str())) {
                    layout->game_asset_directories.push_back(With_Separator_(entry) + name + "game/");
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
            return true;
        }

        std::string root;
        if (platform == PLATFORM_WINDOWS) {
            const char* appdata = Absolute_Variable_("APPDATA");
            if (appdata == nullptr) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "user folder: APPDATA isn't set; using portable mode");
                return false;
            }
            root = With_Separator_(appdata) + FOLDER_NAME + "/";
        } else {
            std::string home;
            if (!Home_(&home)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "user folder: no home folder; using portable mode");
                return false;
            }
            root = home + "Library/Application Support/" + FOLDER_NAME + "/";
        }
        layout->config = root;
        layout->data = root;
        layout->state = root;
        layout->cache = root + "cache/";
        layout->game_asset_directories.push_back(root + "game/");
        layout->game_asset_directories.push_back(root);
        return true;
    }

    std::string Logs_Directory(const std::string& install_directory) {
        s_layout layout;
        if (Resolve(install_directory, CURRENT_PLATFORM, &layout)) {
            return layout.state + "logs/";
        }
        return With_Separator_(install_directory) + "logs/";
    }

    std::string Saves_Directory(const std::string& game_directory) {
        if (user_paths::Is_Enabled()) {
            return user_paths::Get_Directory(user_paths::USER_DIRECTORY_DATA);
        }
        return With_Separator_(game_directory);
    }

    std::string Config_Directory(const std::string& game_directory) {
        if (user_paths::Is_Enabled()) {
            return user_paths::Get_Directory(user_paths::USER_DIRECTORY_CONFIG);
        }
        return With_Separator_(game_directory) + "data/";
    }
} // namespace antares::user_folder

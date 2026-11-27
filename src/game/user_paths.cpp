#include "pch.h"

#include "user_paths.h"

#include "antares/user_folder.h"

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace user_paths {
    namespace {
        enum e_legacy_file_kind {
            LEGACY_FILE_USER,
            LEGACY_FILE_USER_WITH_INSTALL_FALLBACK,
            LEGACY_FILE_INSTALL,
        };

        struct s_legacy_file {
            const char* name;
            e_user_directory directory;
            e_legacy_file_kind kind;
        };

        const s_legacy_file legacy_files[] = {
            {"HOF.M2", USER_DIRECTORY_DATA, LEGACY_FILE_USER},
            {"ORION2RE.RAC", USER_DIRECTORY_DATA, LEGACY_FILE_USER},
            {GAME_SETTINGS_FILENAME, USER_DIRECTORY_CONFIG, LEGACY_FILE_USER},
            {"LANGUAGE.INI", USER_DIRECTORY_CONFIG, LEGACY_FILE_USER_WITH_INSTALL_FALLBACK},
            {"TEMP.TMP", USER_DIRECTORY_CACHE, LEGACY_FILE_USER},
            {"ORIONCD.INI", USER_DIRECTORY_COUNT, LEGACY_FILE_INSTALL},
        };

        struct s_user_paths_state {
            bool enabled;
            std::string session_directory;
            std::string executable_directory;
            std::string installed_data_directory;
            std::string directories[USER_DIRECTORY_COUNT];
            std::vector<std::string> game_asset_directories;
        };

        s_user_paths_state state;

        std::string With_Trailing_Separator_(std::string path) {
            if (path.empty() || path.back() != '/') {
                path += '/';
            }
            return path;
        }

        bool Create_Directories_(const std::string& path) {
#if defined(_WIN32)
            return SDL_CreateDirectory(path.c_str());
#else
            for (size_t index = 1; index <= path.size(); ++index) {
                if (index != path.size() && path[index] != '/') {
                    continue;
                }
                const std::string component = path.substr(0, index);
                if (mkdir(component.c_str(), 0700) != 0 && errno != EEXIST) {
                    return false;
                }
            }
            struct stat info;
            return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
        }

        bool Has_Directory_Component_(const char* path) {
            return strchr(path, '/') != nullptr || strchr(path, '\\') != nullptr || strchr(path, ':') != nullptr;
        }

        const s_legacy_file* Find_Legacy_File_(const char* name) {
            static const s_legacy_file save_game = {"SAVE?.GAM", USER_DIRECTORY_DATA, LEGACY_FILE_USER};
            const size_t length = strlen(name);
            if (length >= 8 && strncasecmp(name, "SAVE", 4) == 0 && strcasecmp(name + length - 4, ".GAM") == 0) {
                return &save_game;
            }
            for (const s_legacy_file& file : legacy_files) {
                if (strcasecmp(name, file.name) == 0) {
                    return &file;
                }
            }
            return nullptr;
        }

        bool Find_Existing_File_(const std::string& directory, const char* name, std::string* path) {
            char resolved_path[1024];
            const std::string candidate = directory + name;
            SDL_PathInfo info;
            if (!Resolve_File_Path_Case(candidate.c_str(), resolved_path, sizeof(resolved_path)) ||
                !SDL_GetPathInfo(resolved_path, &info) || info.type != SDL_PATHTYPE_FILE) {
                return false;
            }
            *path = resolved_path;
            return true;
        }

        bool Find_Install_File_(const char* name, std::string* path) {
            if (Find_Existing_File_(state.executable_directory, name, path) ||
                (!state.installed_data_directory.empty() && Find_Existing_File_(state.installed_data_directory, name, path))) {
                return true;
            }
            for (const std::string& directory : state.game_asset_directories) {
                if (Find_Existing_File_(directory, name, path)) {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    bool Init(const char* executable_directory, bool platform_uses_xdg, const char* installed_data_directory) {
        Shutdown();
        // Antares: the user folder on every platform, with Antares's names (D28, WP5.9).
        antares::user_folder::s_layout layout;
        if (executable_directory == nullptr || !antares::user_folder::Resolve(executable_directory, antares::user_folder::Platform_For(platform_uses_xdg), &layout)) {
            return false;
        }
        state.directories[USER_DIRECTORY_CONFIG] = layout.config;
        state.directories[USER_DIRECTORY_DATA] = layout.data;
        state.directories[USER_DIRECTORY_STATE] = layout.state;
        state.directories[USER_DIRECTORY_CACHE] = layout.cache;
        state.game_asset_directories = layout.game_asset_directories;

        for (const std::string& directory : state.directories) {
            if (!Create_Directories_(directory)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "user paths: unable to create '%s': %s", directory.c_str(), strerror(errno));
            }
        }
        state.executable_directory = executable_directory;
        if (installed_data_directory != nullptr && installed_data_directory[0] != '\0') {
            state.installed_data_directory = With_Trailing_Separator_(installed_data_directory);
        }
        state.enabled = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "user paths: config '%s', data '%s', state '%s', cache '%s'", state.directories[USER_DIRECTORY_CONFIG].c_str(), state.directories[USER_DIRECTORY_DATA].c_str(), state.directories[USER_DIRECTORY_STATE].c_str(), state.directories[USER_DIRECTORY_CACHE].c_str());
        return true;
    }

    void Shutdown() {
        state = {};
    }

    bool Is_Enabled() {
        return state.enabled;
    }

    const char* Get_Directory(e_user_directory directory) {
        if (!state.enabled || directory < 0 || directory >= USER_DIRECTORY_COUNT) {
            return "";
        }
        return state.directories[directory].c_str();
    }

    const std::vector<std::string>& Get_Game_Asset_Directories() {
        return state.game_asset_directories;
    }

    void Set_Session_Directory(const std::string& directory) {
        state.session_directory = directory.empty() ? "" : With_Trailing_Separator_(directory);
    }

    bool Redirect_Legacy_Path(const char* path, bool writing, char* output_path, size_t output_path_size) {
        if ((!state.enabled && state.session_directory.empty()) || path == nullptr || path[0] == '\0' || output_path == nullptr ||
            output_path_size == 0 || Has_Directory_Component_(path)) {
            return false;
        }
        const s_legacy_file* file = Find_Legacy_File_(path);
        if (file == nullptr) {
            return false;
        }

        std::string redirected;
        if (!state.session_directory.empty() && file->kind != LEGACY_FILE_INSTALL) {
            redirected = state.session_directory + path;
        } else if (!state.enabled) {
            return false;
        } else if (file->kind == LEGACY_FILE_INSTALL) {
            if (writing || !Find_Install_File_(path, &redirected)) {
                return false;
            }
        } else {
            redirected = state.directories[file->directory] + path;
            std::string existing;
            if (Find_Existing_File_(state.directories[file->directory], path, &existing)) {
                redirected = existing;
            } else if (file->kind == LEGACY_FILE_USER_WITH_INSTALL_FALLBACK && !writing && Find_Install_File_(path, &existing)) {
                redirected = existing;
            }
        }

        // Do not let a long XDG path silently fall back to writing in the install.
        if (strlcpy(output_path, redirected.c_str(), output_path_size) >= output_path_size) {
            output_path[0] = '\0';
            errno = ENAMETOOLONG;
        }
        return true;
    }
} // namespace user_paths

#pragma once

#include <string>
#include <vector>

// The player's own folder (D28, WP5.9). An installed Battle at Antares (Inno Setup, AppImage,
// Flatpak, the macOS app) has a read-only install folder, so saves, settings, profiles, the
// player's packages, logs and the imported original files live in a per-user folder instead:
//
//   Linux    XDG: ~/.config/antares/, ~/.local/share/antares/ (saves, mods/, game/),
//            ~/.local/state/antares/ (logs/), ~/.cache/antares/   (Redone's split, renamed)
//   Windows  %APPDATA%\Battle at Antares\        (everything; cache\ inside)
//   macOS    ~/Library/Application Support/Battle at Antares/
//
// A portable folder (the zip: writable, no marker) keeps everything beside the program, as before.
// The game (through Redone's user_paths), the launcher and the Godot host all decide with
// Resolve(), so they always agree on where things are.
namespace antares::user_folder {
    enum e_platform {
        PLATFORM_PORTABLE_ONLY, // never a user folder (tests: Redone's "non-XDG platform")
        PLATFORM_LINUX,
        PLATFORM_WINDOWS,
        PLATFORM_MACOS,
    };

#if defined(_WIN32)
    inline constexpr e_platform CURRENT_PLATFORM = PLATFORM_WINDOWS;
#elif defined(__APPLE__)
    inline constexpr e_platform CURRENT_PLATFORM = PLATFORM_MACOS;
#elif defined(__linux__)
    inline constexpr e_platform CURRENT_PLATFORM = PLATFORM_LINUX;
#else
    inline constexpr e_platform CURRENT_PLATFORM = PLATFORM_PORTABLE_ONLY;
#endif

    /** Redone's user_paths::Init flag: true is Linux (XDG); false is this platform's own folder,
     *  or none on Linux (Redone's tests use it for "a platform without XDG"). */
    inline constexpr e_platform Platform_For(bool platform_uses_xdg) {
        if (platform_uses_xdg) return PLATFORM_LINUX;
        return CURRENT_PLATFORM == PLATFORM_LINUX ? PLATFORM_PORTABLE_ONLY : CURRENT_PLATFORM;
    }

    /** Installers put this empty file beside the program to ask for the user folder even when the
     *  install folder is writable (Windows per-user installs). Redone's "xdg" works too. */
    inline constexpr char MARKER_NAME[] = "user-folder";
    /** The folder name on Windows and macOS. */
    inline constexpr char FOLDER_NAME[] = "Battle at Antares";
    /** The XDG folder name on Linux (D1's short id). */
    inline constexpr char XDG_NAME[] = "antares";

    struct s_layout {
        // Each ends with '/'. On Windows and macOS config, data and state are the same folder.
        std::string config; // antares.cfg, profiles/, launcher.json, the game's settings file
        std::string data;   // saves, hall of fame, mods/, game/ (the imported original files)
        std::string state;  // logs/
        std::string cache;
        std::vector<std::string> game_asset_directories; // searched for the original files
    };

    /** Whether the install folder asks for the user folder: ANTARES_USER_FOLDER, ANTARES_XDG or
     *  ORION2RE_XDG ("0" no, anything else yes; empty is unset), else a marker file ("user-folder"
     *  or Redone's "xdg"), else an install folder the player can't write to. */
    bool Selected(const std::string& install_directory);

    /** The user folder for `install_directory` on `platform`, or false for portable mode (not
     *  selected, or no home folder). Reads the environment; creates nothing. */
    bool Resolve(const std::string& install_directory, e_platform platform, s_layout* layout);

    /** Where logs go: the user folder's state/logs/ or <install>/logs/ (portable). */
    std::string Logs_Directory(const std::string& install_directory);

    /** After vfs::Init, when user_paths is enabled: the user config folder (for .cfg files), the
     *  user's packages (mods/, which win over bundled ones of the same id) and the folders
     *  searched for the original files. False when a path is too long for the VFS. */
    bool Mount(const std::string& game_directory);

    /** The folder holding saves (SAVE1-10.GAM): the user data folder, or `game_directory`. */
    std::string Saves_Directory(const std::string& game_directory);
    /** The folder for antares.cfg, profiles/ and launcher.json: the user config folder, or
     *  `game_directory` + "data/" (launcher.json then sits in `game_directory`, see launcher). */
    std::string Config_Directory(const std::string& game_directory);
} // namespace antares::user_folder

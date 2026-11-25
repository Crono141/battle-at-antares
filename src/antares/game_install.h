#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "antares/save_summary.h"

// The setup wizard's work (WP5.11, D28): find the player's Master of Orion II, check it, copy its
// files into Antares's own folder and import its saves. The launcher draws the pages
// (src/launcher/launcher_setup.cpp); everything here is plain file work, unit-tested on fake trees.
namespace antares::game_install {
    /** A folder that holds Master of Orion II, found by Detect(). */
    struct s_candidate {
        std::string path;   // the folder with the LBX files, ending in '/'
        std::string source; // where it was found: "Steam", "GOG", "GOG Galaxy", "Heroic", "Wine", ...
        size_t found = 0;   // how many of game_files::Required_Files() it has
    };

    /** Where to look. Detect_Roots() fills it for this machine; tests build their own. */
    struct s_root {
        std::string path;   // a folder to search below
        std::string source; // the label for what's found there
        int depth = 2;      // how many folder levels below `path` the game may sit
    };

    /** The usual places on this platform: Steam libraries (libraryfolders.vdf, native and
     *  Flatpak), GOG and Galaxy folders, Heroic's installed list, Lutris and Wine prefixes, and on
     *  Windows the folders the caller read from the registry (`registry_roots`). */
    std::vector<s_root> Detect_Roots(const std::string& home, const std::vector<s_root>& registry_roots = {});

    /** Every folder below the roots that holds DIPLOMAT.LBX, best first (most required files),
     *  without duplicates. Bounded: a few thousand folders at most. */
    std::vector<s_candidate> Detect(const std::vector<s_root>& roots);

    /** Steam's store id for Master of Orion 2 (Wargaming Labs; checked 2026-11-24 on the owner's copy). */
    inline constexpr int STEAM_APP_ID = 410980;

    /** Steam's own folders on this machine (its install, and each library from libraryfolders.vdf),
     *  native, Flatpak and macOS; `registry_roots` adds Windows' SteamPath. Empty: no Steam. */
    std::vector<std::string> Steam_Libraries(const std::string& home, const std::vector<s_root>& registry_roots = {});

    /** What Steam says about an app (its appmanifest_<id>.acf in any library). */
    struct s_steam_app {
        bool known = false;     // a manifest exists: Steam is installing or has installed it
        bool installed = false; // StateFlags has "fully installed" and no update is pending
        uint64_t done = 0;      // BytesDownloaded
        uint64_t total = 0;     // BytesToDownload
        std::string folder;     // <library>/steamapps/common/<installdir>/
    };
    s_steam_app Steam_App(const std::vector<std::string>& libraries, int app_id);

    /** A value from Steam's KeyValues text ("key" "value"), first match; empty when absent. */
    std::string Vdf_Value(const std::string& text, const std::string& key);

    /** Steam's libraryfolders.vdf: each library's "path". */
    std::vector<std::string> Parse_Steam_Libraries(const std::string& vdf_text);

    /** The checklist for one folder. */
    struct s_folder_check {
        std::string folder; // ends in '/'
        bool exists = false;
        std::vector<std::string> missing; // required files not there
        std::vector<std::string> empty;   // required files there but empty (a broken copy)
        bool patched = false;             // PATCH13.LBX: version 1.31
        std::vector<std::string> to_copy; // every LBX file in it, as named on disk
        uint64_t bytes = 0;               // their size
        size_t found = 0;                 // required files present (and not empty)
        bool ok() const {
            return exists && missing.empty() && empty.empty();
        }
    };

    /** Checks `folder`. When it doesn't hold the game but one folder below does (a bundle such as
     *  GOG's Master of Orion 1+2, or Steam's), that folder is checked instead. */
    s_folder_check Check_Folder(const std::string& folder);

    /** Where the original files go: game/ in the user folder when installed, else game/ in the game
     *  folder (Mount searches both). */
    std::string Target_Folder(const std::string& game_dir);

    /** Shared between the copy thread and the window. */
    struct s_copy_progress {
        std::atomic<uint64_t> bytes_done{0};
        std::atomic<uint64_t> bytes_total{0};
        std::atomic<bool> cancel{false};
        std::atomic<bool> finished{false};
        std::atomic<bool> failed{false};
        std::mutex mutex;
        std::string current; // the file being copied
        std::string error;
    };

    /** Copies check.to_copy into `target` (created if needed), each through a temporary name, so a
     *  cancelled or failed copy never leaves a half file under the real name. Files already there
     *  with the same size are kept. Does nothing when `target` is the checked folder itself. */
    bool Copy_Files(const s_folder_check& check, const std::string& target, s_copy_progress* progress);

    /** A save found in the player's Master of Orion II. */
    struct s_found_save {
        int slot = 0;
        std::string path;
        saves::s_save_summary summary;
        bool taken = false; // Antares already has a save in that slot
    };

    /** SAVE1-10.GAM in `folder`, in GOG's cloud_saves/ beside them, and (Windows) in the
     *  VirtualStore copy of the folder (`virtual_store`, empty elsewhere); the newest copy of a slot
     *  wins. `saves_dir` is Antares's save folder, to mark slots already taken. */
    std::vector<s_found_save> Find_Saves(const std::string& folder, const std::string& virtual_store, const std::string& saves_dir);

    /** Copies the saves into `saves_dir`. A taken slot is replaced only with `replace_taken`
     *  (owner, question 22: one choice, keep Antares's saves or the original's). Returns how many
     *  were imported; `error` says what failed. */
    int Import_Saves(const std::vector<s_found_save>& saves, const std::string& saves_dir, bool replace_taken, std::string* error);

    /** Windows: %LOCALAPPDATA%\VirtualStore\<folder without its drive>, where Windows put files a
     *  program wrote into Program Files; empty elsewhere or when not set. */
    std::string Virtual_Store_For(const std::string& folder);
} // namespace antares::game_install

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// The HD art packs (WP5.14, D28): listed from the battle-at-antares-hd release on GitHub, downloaded
// with the system's own HTTPS (curl on Linux and macOS, WinHTTP on Windows; no login, no library
// of ours), checked against the SHA-256 GitHub publishes for each file, and put in the player's
// mods/ folder as the art-only packages they are (D18: saves never need them).
namespace antares::hd_packs {
    inline constexpr char RELEASE_API_URL[] = "https://api.github.com/repos/Crono141/battle-at-antares-hd/releases/latest";
    inline constexpr char STATE_FILE[] = "hd-packs.json"; // in the mods folder: id -> installed sha256

    struct s_pack {
        std::string id;          // the package id: the zip's name without ".zip"
        std::string file;        // "antares-hd.zip"
        std::string title;       // for people
        std::string description; // one line
        uint64_t size = 0;       // bytes
        std::string sha256;      // lower-case hex, from GitHub's "digest"
        std::string url;
        bool recommended = false; // the one to start with
    };

    /** The packs in a GitHub release (the JSON of RELEASE_API_URL), in the order to offer them;
     *  zips only. `tag` gets the release's tag. False with `error` when the text isn't a release. */
    bool Parse_Release(const std::string& json, std::vector<s_pack>* packs, std::string* tag, std::string* error);

    /** The mods folder packs go in: the user folder's mods/ when installed, else <game>/data/mods/. */
    std::string Mods_Folder(const std::string& game_dir);

    enum e_state { NOT_INSTALLED,
        INSTALLED,
        UPDATE,
        OTHER_COPY };
    /** NOT_INSTALLED, INSTALLED (the zip is there and the state file has this pack's checksum),
     *  UPDATE (the state file has another checksum), OTHER_COPY (a zip or folder of that id that
     *  this launcher didn't download: left alone). */
    e_state State(const s_pack& pack, const std::string& mods_folder);

    /** Records a finished download in the state file. */
    bool Record_Installed(const std::string& mods_folder, const s_pack& pack, std::string* error);

    /** A file's SHA-256 (lower-case hex), read in pieces; `progress` gets the bytes done and may
     *  return false to stop (then the result is empty). */
    std::string File_Sha256(const std::string& path, const std::function<bool(uint64_t)>& progress = {});

    /** Progress shared between a download thread and the window. */
    struct s_progress {
        std::atomic<uint64_t> done{0};
        std::atomic<uint64_t> total{0};
        std::atomic<bool> cancel{false};
    };

    /** A small download into memory (the release list). Defined in the launcher
     *  (src/launcher/launcher_fetch.cpp), like Fetch and Install. */
    bool Fetch_Text(const std::string& url, std::string* text, std::string* error);

    /** Downloads `url` to `path` (resuming a partial file already there), blocking; `progress`
     *  follows the bytes. False with `error` on failure or cancel. */
    bool Fetch(const std::string& url, const std::string& path, s_progress* progress, std::string* error);

    /** Downloads `pack` into `mods_folder` through <file>.part, checks its SHA-256, renames it to
     *  <file> and records it. `phase` says what is happening ("Downloading", "Checking"). */
    bool Install(const s_pack& pack, const std::string& mods_folder, s_progress* progress, std::string* phase, std::mutex* phase_mutex, std::string* error);
} // namespace antares::hd_packs

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Updates from the launcher (WP5.19, owner 2026-11-24): the latest Battle at Antares release on
// GitHub, compared with this copy's release label, and the file of the same kind of package as
// this copy (the Windows installer, the Windows zip, the Linux archive, the AppImage...). A copy
// without a release label (version.txt, written by the release script) is a development build
// and never updates itself.
namespace antares::updates {
    inline constexpr char RELEASE_API_URL[] = "https://api.github.com/repos/Crono141/battle-at-antares/releases/latest";
    inline constexpr char VERSION_FILE[] = "version.txt"; // in the program's folder

    struct s_asset {
        std::string name;
        std::string url;
        uint64_t size = 0;
        std::string sha256; // lower-case hex, from GitHub's "digest"
    };

    struct s_release {
        std::string version;  // the tag without its "v"
        std::string page_url; // the release's page
        std::string intro;    // the notes' first paragraph, plain text
        std::vector<s_asset> assets;
    };

    /** A GitHub release (the JSON of RELEASE_API_URL). False with `error` when it isn't one. */
    bool Parse_Release(const std::string& json, s_release* release, std::string* error);

    /** Release labels compared as semantic versions: -1, 0 or 1. A pre-release label
     *  ("0.8.0-test12") comes before its release ("0.8.0"); its parts compare as numbers where
     *  both are numbers ("test9" < "test10"). */
    int Compare_Versions(const std::string& a, const std::string& b);

    enum e_kind { KIND_NONE,
        KIND_WINDOWS_INSTALLER,
        KIND_WINDOWS_ZIP,
        KIND_LINUX_ARCHIVE,
        KIND_APPIMAGE,
        KIND_FLATPAK,
        KIND_MAC_APP,
        KIND_MAC_ZIP };

    /** What kind of package this copy is, from its folder and environment. */
    e_kind Package_Kind(const std::string& program_dir);

    /** Whether the launcher can install this kind itself (else it offers the release page). */
    bool Can_Install(e_kind kind);

    /** This kind's file in `release` (by its name's ending), or nullptr. */
    const s_asset* Asset_For(const s_release& release, e_kind kind);

    /** The release label in `program_dir`'s version.txt, or "" (a development build). */
    std::string Installed_Version(const std::string& program_dir);

    /** For the swap of a portable copy: whether `relative` (a path in the archive, '/'
     *  separated) is the player's once it exists (data/antares.cfg), so it is never replaced. */
    bool Players_File(const std::string& relative);
} // namespace antares::updates

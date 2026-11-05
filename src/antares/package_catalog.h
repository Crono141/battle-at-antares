#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/config_lifecycle.h"

#include "config/mod_manifest.h"

// The launcher's view of the packages installed in data/mods/ (WP5.1, D23). The game itself only
// reads the packages a config enables; the launcher has to list all of them, including those that
// aren't mounted, and check a profile without making it active.
namespace antares {
    struct s_package_info {
        std::string id;          // folder name, or the zip's name without ".zip"
        std::string source_path; // the folder (ending in '/') or the zip on disk
        bool archive = false;
        bool valid = false;    // the manifest parsed and passed the game's checks
        std::string error;     // why not, when !valid ("file:line:column: message")
        bool art_only = false; // D18: not recorded in saves
        config::s_mod_manifest manifest;
    };

    /** Lists every package in <vfs base>/data/mods/, sorted by id. Needs vfs::Init. A package that
     *  exists both as a folder and as a zip is listed once, with the game's error. */
    std::vector<s_package_info> Discover_Packages();

    struct s_config_check {
        bool ok = false;
        std::vector<std::string> package_order; // highest priority first
        std::string error;                      // "file:line:column: message" when !ok
        // Every setting's value and where it came from, when ok (WP5.2).
        std::shared_ptr<const config::s_effective_config> effective;
    };

    /** A dry run of the game's own load of `entry_path` (a VFS path such as
     *  "profiles/hd.cfg"): resolves packages, dependencies, conflicts and settings, then
     *  discards it all. */
    s_config_check Check_Config(const std::string& entry_path);

    /** "file:line:column: message", leaving out the parts that are empty or zero. */
    std::string Format_Diagnostic(const config::s_cfg_diagnostic& diagnostic);
} // namespace antares

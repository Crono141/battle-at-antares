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
        bool hd_art = false;   // has an HD art pack (data/art/ or data/hd/ with pack.json or index.json)
        bool godot_ui = false; // has Godot resource packs (godot/*.pck or *.zip): a UI package (D26)
        std::string readme;    // README.md at the package root, if any (WP5.5)
        config::s_mod_manifest manifest;
    };

    /** Lists every package in <vfs base>/data/mods/, sorted by id. Needs vfs::Init. A package that
     *  exists both as a folder and as a zip is listed once, with the game's error. */
    std::vector<s_package_info> Discover_Packages();

    /** One mounted package's copy of a file. */
    struct s_package_file {
        std::string package_id;
        std::string bytes;
        std::string disk_directory; // the file's folder on disk ('/'-separated, no trailing '/'); empty in a zip
    };

    /** `relative` (a '/'-separated path inside a package, e.g. "data/art/index.json") from every
     *  mounted package that has it, highest priority first. Unlike a VFS read, which gives only
     *  the highest-priority copy, this lets files that several packages each carry be merged:
     *  the art index, so an art pack split over several packages (or two art packs from
     *  different authors) all show (release 0.1). Needs the packages mounted. */
    std::vector<s_package_file> Read_Mounted_Package_Files(const char* relative);

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

    /** A dry run of a config that enables only `package_id`: the settings it and its dependencies
     *  set, with `effective->provenance` naming the package of each (WP5.5). */
    s_config_check Check_Package(const std::string& package_id);

    /** A dry run of loading a save's configuration with the profile `entry_path`: the packages the
     *  save recorded (all of them when `required_mods` is null: the profile's own), plus the art
     *  packages the profile enables, with the save's values. `error` is what the game would say. */
    s_config_check Check_Save(const std::string& entry_path, const std::vector<config::s_mod_requirement>* required_mods, const config::s_config_state* state);

    /** Turning one package on or off in a profile's enable list, with what had to change too. */
    struct s_package_toggle {
        std::vector<std::string> enabled;   // the new enable list, highest priority first
        std::vector<std::string> removed;   // other packages switched off, in list order
        std::vector<std::string> reasons;   // one line per removed package, same order
    };

    /** Switches `package_id` on (appended at the lowest priority) or off in `enabled`. On: every
     *  enabled package that would conflict with it is switched off, comparing what each loads
     *  (itself, its dependencies and installed optional dependencies, as the game resolves them; a
     *  conflict declared on either side counts). Off: every enabled package that would still load
     *  it as a dependency is switched off too, since the game would load it anyway. */
    s_package_toggle Toggle_Package(const std::vector<s_package_info>& packages, const std::vector<std::string>& enabled, const std::string& package_id, bool on);

    /** "file:line:column: message", leaving out the parts that are empty or zero. */
    std::string Format_Diagnostic(const config::s_cfg_diagnostic& diagnostic);
} // namespace antares

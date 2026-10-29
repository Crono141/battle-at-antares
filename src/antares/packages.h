#pragma once

#include <string>

#include "config/cfg_parser.h"
#include "config/mod_manifest.h"

// Art-only packages (D18). A package whose only content is art under data/hd/ or data/art/ (no
// config statements, no Lua, no dependencies, nothing else in data/) changes how the game looks,
// not how it plays. Such packages are left out of the package list stored in saves and network
// snapshots, and the art packages the player has enabled stay active when a save is loaded.
namespace antares {
    /** True when the package has no rules: empty config, no Lua, no dependencies, and its files
     *  are only mod.json5, its config file and folders data/hd/ and data/art/. */
    bool Is_Art_Only_Package(const config::s_mod_manifest& manifest, const config::s_cfg_document& document);

    /** Same test for a package that isn't mounted yet, read straight from data/mods/. Used on
     *  load, before the resolver stages any mounts. False when the package can't be read. */
    bool Is_Art_Only_Package(const std::string& package_id);
} // namespace antares

#pragma once

#include <string>

#include "config/cfg_parser.h"
#include "config/mod_manifest.h"
#include "lua/lua_save.h"

// Art-only packages (D18). A package whose only content is art under data/hd/ or data/art/ (no
// config statements, no Lua, no dependencies, nothing else in data/) changes how the game looks,
// not how it plays. Such packages are left out of the package list stored in saves and network
// snapshots, and the art packages the player has enabled stay active when a save is loaded.
namespace antares {
    /** True when the package has no rules: empty config, no Lua, no dependencies, and its files
     *  are only mod.json5, README.md, its config file and folders data/hd/ and data/art/. */
    bool Is_Art_Only_Package(const config::s_mod_manifest& manifest, const config::s_cfg_document& document);

    /** Same test for a package that isn't mounted yet, read straight from data/mods/. Used on
     *  load, before the resolver stages any mounts. False when the package can't be read. */
    bool Is_Art_Only_Package(const std::string& package_id);

    /** Art-only packages that were renamed (WP2.4, 2026-11-04: hd_textures became antares-hd). Saves
     *  made before D18 still list them; they only ever held art, so a save never needs them, even
     *  when they are no longer installed. */
    bool Is_Retired_Art_Package(const std::string& package_id);

    /** Saves made before D18 (the legacy orion2re-hd build) list art packs in their Lua
     *  snapshot. Returns a copy without the art-only packages that the active Lua registry
     *  doesn't have, or `snapshot` itself when there are none. */
    const orion2re::lua::s_lua_save_snapshot* Without_Art_Packages(const orion2re::lua::s_lua_save_snapshot* snapshot, const orion2re::lua::s_lua_registry& active, orion2re::lua::s_lua_save_snapshot* storage);
} // namespace antares

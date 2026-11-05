#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "config/config_runtime.h"
#include "config/mod_manifest.h"

// What the launcher shows about a save (WP5.3): its header and the packages it recorded, read
// without loading the game. Saves record every package that changes the rules, in priority order
// (D18: not art-only ones); loading a save stages exactly those, plus the art packages the profile
// enables, whatever else the profile says.
namespace antares::saves {
    inline constexpr int k_slot_count = 10; // SAVE1.GAM to SAVE10.GAM; 10 is the autosave

    struct s_save_summary {
        int slot = 0;              // 1-10
        bool exists = false;       // the file is there
        bool readable = false;     // the header and the package list were read
        std::string error;         // why not, when it exists but isn't readable
        std::string description;   // the save's name, UTF-8, colour codes removed
        int32_t stardate = 0;      // 35004 is 3500.4
        int game_type = 0;         // 0 single player, 1 hot seat, 2 and 3 network
        uint16_t version = 0;      // save format version; 0 for the original game's saves
        int64_t modified = 0;      // file time, SDL_Time nanoseconds
        bool records_packages = false; // false: it loads with the profile's packages (older saves)
        std::vector<config::s_mod_requirement> packages; // highest priority first
        std::shared_ptr<const config::s_config_state> state; // the save's rule values, if it has them
    };

    /** Reads a save file's bytes. False (with `error`) when it isn't a save this engine can read. */
    bool Read_Save_Summary(const uint8_t* data, size_t size, s_save_summary* summary);

    /** Reads SAVE<slot>.GAM in the game folder (the VFS base). */
    s_save_summary Load_Save_Summary(int slot);

    /** "3500.4". */
    std::string Format_Stardate(int32_t stardate);

    /** True for network games, which load only from the multiplayer menu, not with -load-save. */
    inline bool Is_Network_Game(const s_save_summary& summary) {
        return summary.game_type == 2 || summary.game_type == 3;
    }
} // namespace antares::saves

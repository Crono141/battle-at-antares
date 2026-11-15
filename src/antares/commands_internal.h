#pragma once

// Shared by the files that fill the command table (commands.cpp, galaxy_queries.cpp).

#include "antares/commands.h"

#include <string>
#include <vector>

namespace antares::commands::internal {

    // Game text (CP850, with colour and layout control bytes) as clean UTF-8.
    std::string Utf8(const char* text, size_t max_length);
    // An application's help text (HELP.LBX), empty if none (colony_queries.cpp).
    std::string Application_Help(int16_t app);
    // Fails `out` with "no_game" when no game is loaded.
    bool Have_Game(s_out& out);

    // WP3.3a: the galaxy's queries and commands (galaxy_queries.cpp).
    void Add_Galaxy_Entries(std::vector<s_entry>* table);
    // WP3.3c: the fleet box and fleet orders (fleet_orders.cpp).
    void Add_Fleet_Entries(std::vector<s_entry>* table);
    // WP3.4: the colony pane (colony_queries.cpp).
    void Add_Colony_Entries(std::vector<s_entry>* table);
    void Add_Planet_Entries(std::vector<s_entry>* table);
    void Add_Save_Entries(std::vector<s_entry>* table);
    // WP3.4c: `preview_popup`, a developer verb (popup_preview.cpp).
    void Add_Preview_Entries(std::vector<s_entry>* table);
    // WP3.4d: the to-do list's query and commands (todo.cpp).
    void Add_Todo_Entries(std::vector<s_entry>* table);
    // A leader's card for native views and popups (galaxy_queries.cpp): name, title, level, type,
    // portrait, skills with bonuses for `owner`.
    void Leader_Card(int16_t leader_idx, int16_t owner, s_out g);
    std::string Leader_Card_Json(int16_t leader_idx, int16_t owner);
    // A system special's description (the discovery window's text), read from its file.
    std::string Special_Description(int16_t special);
    // The `system` query's answer for a star, as JSON (popups that zoom into a system).
    std::string System_Json(int16_t star);
    // The `potential_colony` answer for a planet as JSON, or "" for a gas giant (popups carry it).
    std::string Potential_Json(int16_t planet);
    // The `research` query's answer as JSON (the research window's picker carries it).
    std::string Research_Json();
    // A research choice as the change-research screen commits one; false if it isn't one of the
    // choices.
    bool Apply_Research_Choice(int32_t field, int32_t app);

} // namespace antares::commands::internal

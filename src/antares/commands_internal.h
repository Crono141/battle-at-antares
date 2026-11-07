#pragma once

// Shared by the files that fill the command table (commands.cpp, galaxy_queries.cpp).

#include "antares/commands.h"

#include <string>
#include <vector>

namespace antares::commands::internal {

    // Game text (CP850, with colour and layout control bytes) as clean UTF-8.
    std::string Utf8(const char* text, size_t max_length);
    // Fails `out` with "no_game" when no game is loaded.
    bool Have_Game(s_out& out);

    // WP3.3a: the galaxy's queries and commands (galaxy_queries.cpp).
    void Add_Galaxy_Entries(std::vector<s_entry>* table);

} // namespace antares::commands::internal

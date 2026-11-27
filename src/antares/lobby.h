#pragma once
// WP3.13 (D30): the title screen, the new game and the Hall of Fame as native views (lobby.cpp).

#include <cstdint>

namespace antares::lobby {
    // What the legacy screen does on entry that isn't drawing (the native loop calls it).
    void Enter(int16_t screen);
    // The Hall of Fame entry to flash: the new entry at the end of a game, or -1.
    int16_t Flash_Entry();
    void Set_Flash_Entry(int16_t entry);
} // namespace antares::lobby

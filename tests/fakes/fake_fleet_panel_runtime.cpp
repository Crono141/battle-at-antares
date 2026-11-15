#include "pch.h"

// Stand-ins for what src/antares/fleet_panel_queries.cpp calls in the game (WP3.5c), for
// tests/unit/fleet_panel_queries_test.cpp.

namespace TEST_FLEET_PANEL {
    int relocations = 0;
    int16_t last_from = -1;
    int16_t last_to = -1;
} // namespace TEST_FLEET_PANEL

namespace FLT1 {
    // The screen's two clicks: the first star is taken, the second sets (or cancels) it.
    void __cdecl Star_Relocation(int16_t* from, int16_t* to, int16_t star) {
        if (*from == -1) {
            *from = star;
            return;
        }
        ++TEST_FLEET_PANEL::relocations;
        TEST_FLEET_PANEL::last_from = *from;
        TEST_FLEET_PANEL::last_to = star;
        *from = -1;
        *to = -1;
    }
} // namespace FLT1

namespace FLT2 {
    char* __cdecl Crew_Description_String(int16_t) {
        static char text[] = "Regular Crew";
        return text;
    }
} // namespace FLT2

namespace INITSHIP {
    void __cdecl Get_Ship_Combat_Bonuses(int16_t, int16_t* defense, int16_t* missile_defense, int16_t* attack) {
        *defense = 75;
        *missile_defense = 50;
        *attack = 40;
    }
} // namespace INITSHIP

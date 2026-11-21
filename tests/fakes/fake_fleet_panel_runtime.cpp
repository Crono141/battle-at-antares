#include "pch.h"
#include "game/legacy_ids.h"

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
    // The select-combat window's ships (combat_fleet, 2026-11-20).
    int16_t __cdecl Get_Ship_Armor_Hits(int16_t) {
        return 100;
    }
    int16_t __cdecl Get_Ship_Structure(int16_t) {
        return 60;
    }
} // namespace INITSHIP

namespace DESIGN {
    void __cdecl Weapon_Arc_String(uint16_t, char* dest_str, int32_t dest_str_size) {
        strlcpy(dest_str, "F", (size_t)dest_str_size);
    }
} // namespace DESIGN

// The strength bands: each of our ships rates 30 against a side, each of theirs 10 against us.
namespace AIPOWER {
    uint16_t Ship_Strength_Vs_Ships(const game::s_view*, game::id_ship attacker_idx, game::id_player) {
        return MOX::ship[legacy_world::Slot(attacker_idx)].owner == MOX::PLAYER_NUM ? 30 : 10;
    }
} // namespace AIPOWER

// The system pane's relocation (relocation_targets / _set / _clear, 2026-11-20).
namespace TEST_FLEET_PANEL {
    int16_t relocation_from = -1;
    int16_t relocation_to = -1;
    bool refuse_relocation = false;
} // namespace TEST_FLEET_PANEL

namespace MOX {
    s_mouse_list mouse_list_arrow{};
} // namespace MOX

namespace mouse {
    void __cdecl Set_Mouse_List(s_mouse_list*, int16_t) {
    }
} // namespace mouse

namespace GEO {
    int32_t __cdecl Parsecs_Between_Stars(int16_t a, int16_t b) {
        return a > b ? a - b : b - a;
    }
} // namespace GEO

namespace FLT1 {
    bool __cdecl Okay_To_Set_Relocate_Star(int16_t, uint8_t) {
        return !TEST_FLEET_PANEL::refuse_relocation;
    }
    uint8_t __cdecl Okay_To_Set_Relocate_To_Star(int16_t, int16_t) {
        return TEST_FLEET_PANEL::refuse_relocation ? 0 : 1;
    }
} // namespace FLT1

namespace HACCESS {
    void __cdecl Set_Relocation(int16_t star, int16_t, int16_t target) {
        TEST_FLEET_PANEL::relocation_from = star;
        TEST_FLEET_PANEL::relocation_to = target;
    }
    void __cdecl Clear_Star_Relocation(int16_t star, int16_t) {
        TEST_FLEET_PANEL::relocation_from = star;
        TEST_FLEET_PANEL::relocation_to = -1;
    }
} // namespace HACCESS

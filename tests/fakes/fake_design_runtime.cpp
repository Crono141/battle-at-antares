#include "pch.h"

// Stand-ins for what src/antares/design_queries.cpp calls in the game (WP3.6, the Ship Design
// panel), for tests/unit/design_queries_test.cpp: a small design model (a 31-space hull, each
// weapon 3 space and 1 BC a unit, each special 5 space), the picker lists the bridge reads, and
// the slot written on BUILD. The rules themselves are the game's and run in the Godot smoke.

namespace MOX {
    s_current_design* design = nullptr;
    s_weapon_filter_item* weapon_filter = nullptr;
    int16_t design_choice_items[40];
    int16_t design_choice_item_selected[40];
    int16_t design_screen_replacement_slot = 0;
    int16_t weapon_mod_field_status[WEAPON_MOD_COUNT];
} // namespace MOX

namespace TECHDATA {
    s_tech_weapon_data weapons[WEAPON_COUNT];
    s_tech_weapon_mod_data weapon_modifications[WEAPON_MOD_COUNT];
} // namespace TECHDATA

namespace DESIGN {
    uint8_t changed_design_name = 0;
    uint8_t use_augmented_engines = 1;
    bool missile_default_x2_rack = false;

    void __cdecl Get_Tech_Description_Lbx_Name(char* buffer, int32_t size) {
        strlcpy(buffer, "TECHDESC.LBX", (size_t)size);
    }
    void __cdecl Build_Initial_Design_Template(int16_t player_idx, int16_t slot) {
        *MOX::design = s_current_design{};
        const s_ship_design& stored = MOX::player[player_idx].ship_designs[slot];
        strlcpy(MOX::design->name, stored.name, sizeof(MOX::design->name));
        MOX::design->ship_size = stored.size;
        MOX::design->picture_type = (int16_t)(stored.size * 8);
        MOX::design->weapon_type[0] = 1;
        MOX::design->weapon_count[0] = 1;
        MOX::design->weapon_firing_arc[0] = WEAPON_FIRING_ARC_FORWARD;
    }
    int32_t __cdecl Weapon_Space(int16_t, int16_t, int16_t quantity, int16_t, int16_t, int16_t) {
        return quantity * 3;
    }
    int16_t __cdecl Weapon_Cost(int16_t, int16_t count, int16_t, int16_t, int16_t) {
        return count;
    }
    void __cdecl Update_Calculated_Design_Data(int16_t player_idx) {
        s_current_design* d = MOX::design;
        d->hull_space = 31;
        d->space_used = 0;
        d->total_cost = 10;
        for (int i = 0; i < 8; ++i) {
            if (d->weapon_type[i] > 0) {
                d->weapon_space[i] = Weapon_Space(player_idx, d->weapon_type[i], d->weapon_count[i], 0, 0, 0);
                d->weapon_cost[i] = Weapon_Cost(d->weapon_type[i], d->weapon_count[i], 0, 0, 0);
                d->space_used += d->weapon_space[i];
                d->total_cost += d->weapon_cost[i];
            }
            if (d->special_devices[i] > 0) {
                d->special_device_space[i] = 5;
                d->space_used += 5;
            }
        }
    }
    void __cdecl Clear_Weapon_Slot(int16_t slot) {
        for (int i = slot; i < 7; ++i) {
            MOX::design->weapon_type[i] = MOX::design->weapon_type[i + 1];
            MOX::design->weapon_count[i] = MOX::design->weapon_count[i + 1];
        }
        MOX::design->weapon_type[7] = 0;
        MOX::design->weapon_count[7] = 0;
    }
    void __cdecl Clear_Special_System_Slot(int16_t slot) {
        for (int i = slot; i < 7; ++i) {
            MOX::design->special_devices[i] = MOX::design->special_devices[i + 1];
        }
        MOX::design->special_devices[7] = 0;
    }
    void __cdecl Clear_Design() {
        for (int i = 0; i < 8; ++i) {
            MOX::design->weapon_type[i] = 0;
            MOX::design->weapon_count[i] = 0;
            MOX::design->special_devices[i] = 0;
        }
    }
    void __cdecl Update_Player_Design(int16_t player_idx, int16_t slot) {
        s_ship_design& stored = MOX::player[player_idx].ship_designs[slot];
        strlcpy(stored.name, MOX::design->name, sizeof(stored.name));
        stored.cost = (int16_t)MOX::design->total_cost;
    }
    void __cdecl Get_Ship_Design_Name(int16_t, int16_t size, int16_t picture, char* out, int32_t size_out) {
        snprintf(out, (size_t)size_out, "Hull %d-%d", size, picture);
    }
    int16_t __cdecl Weapon_Type(int16_t weapon) {
        return weapon == 3 ? WEAPON_TYPE_MISSILE : WEAPON_TYPE_BEAM;
    }
    int16_t __cdecl Weapon_Needs_Racks(int16_t weapon) {
        return weapon == 3 ? 1 : 0;
    }
    int16_t Missile_Rack_Quantity(int16_t rack) {
        return (int16_t)(2 + rack * 3);
    }
    int32_t __cdecl Total_Design_Space() {
        return 31 + MOX::design->ship_size * 40;
    }
    uint8_t __cdecl Current_Design_Combat_Speed(s_current_design*) {
        return 24;
    }
    int16_t __cdecl Current_Design_Structural_Points() {
        return 40;
    }
    int16_t __cdecl Current_Design_Armor_Absorption() {
        return 40;
    }
    int16_t __cdecl Shield_Strength() {
        return 0;
    }
    int16_t __cdecl Current_Shield_Damage_Points_Blocked() {
        return 0;
    }
    void __cdecl Build_Design_Template(s_ship_design* out) {
        *out = s_ship_design{};
    }
    int16_t __cdecl Printed_Design_Cost() {
        return (int16_t)MOX::design->total_cost;
    }
    void __cdecl Design_Weapon_Damage_String(int16_t, char* buffer, int32_t size, int16_t, int16_t) {
        strlcpy(buffer, "1-4", (size_t)size);
    }
    int16_t __cdecl Shield_Cost(int16_t) {
        return 1;
    }
    int16_t __cdecl Shield_Space(int16_t, int16_t) {
        return 1;
    }
    int16_t __cdecl Computer_Cost(int16_t) {
        return 1;
    }
} // namespace DESIGN

namespace INITSHIP {
    void __cdecl Get_Design_Combat_Bonuses(s_ship_design, int16_t* attack, int16_t* defense, int16_t* evasion) {
        *attack = 25;
        *defense = 10;
        *evasion = 0;
    }
} // namespace INITSHIP

namespace DESBOX {
    int16_t field_item_chosen = -1;
    uint8_t beam_filter_button_status = 1;
    uint8_t missile_filter_button_status = 1;
    uint8_t bomb_filter_button_status = 1;
    uint8_t special_filter_button_status = 1;
    int16_t weapon_replacement_arcs = 0;
    uint16_t weapon_replacement_rack = 0;
    uint8_t valid_mods[16];

    // Weapons 1 (beam) and 3 (missile) are researched; weapon 2 isn't.
    int16_t __cdecl Weapon_Choices(int16_t) {
        MOX::weapon_filter[0].weapon_idx = 0;
        MOX::weapon_filter[1].weapon_idx = 1;
        MOX::weapon_filter[2].weapon_idx = 3;
        MOX::scroll_bar->total_rows = 3;
        return 2;
    }
    int16_t __cdecl Valid_Modifications(uint8_t* status) {
        memset(status, 0, WEAPON_MOD_COUNT);
        status[WEAPON_MOD_HEAVY_MOUNT] = 1;
        status[WEAPON_MOD_POINT_DEFENSE] = 1;
        return 2;
    }
    int8_t __cdecl Mod_Allowed_On_Weapon_Type(uint8_t) {
        return 1;
    }
    void __cdecl Update_Weapon_Replacement(int16_t* done, int16_t row, int16_t slot) {
        MOX::design->weapon_type[slot] = MOX::weapon_filter[row].weapon_idx;
        MOX::design->weapon_count[slot] = 1;
        MOX::design->weapon_firing_arc[slot] = (int8_t)weapon_replacement_arcs;
        uint16_t mods = 0;
        for (int i = 1; i < WEAPON_MOD_COUNT; ++i) {
            if (MOX::weapon_mod_field_status[i] == 1) {
                mods = (uint16_t)(mods | (1 << i));
            }
        }
        MOX::design->weapon_specials[slot] = mods;
        MOX::current_screen = SCREEN_DESIGN;
        *done = 1;
    }
    // Specials 2 and 3 are researched; 3 can't go with 2.
    int16_t __cdecl Special_System_Choices() {
        MOX::design_choice_items[0] = 0;
        MOX::design_choice_items[1] = 2;
        MOX::design_choice_items[2] = 3;
        for (int i = 0; i < 3; ++i) {
            MOX::design_choice_item_selected[i] = 0;
            for (int s = 0; s < 8; ++s) {
                if (i > 0 && MOX::design->special_devices[s] == MOX::design_choice_items[i]) {
                    MOX::design_choice_item_selected[i] = 1;
                }
            }
        }
        MOX::scroll_bar->total_rows = 3;
        return 2;
    }
    int16_t __cdecl Special_System_Exclusion(int16_t special, int16_t* conflict) {
        *conflict = 0;
        if (special == 3) {
            for (int s = 0; s < 8; ++s) {
                if (MOX::design->special_devices[s] == 2) {
                    *conflict = 2;
                    return 1;
                }
            }
        }
        return 0;
    }
    int16_t __cdecl Items_Selected() {
        int16_t n = 0;
        for (int s = 0; s < 8; ++s) {
            n += MOX::design->special_devices[s] > 0 ? 1 : 0;
        }
        return n;
    }
    void __cdecl System_Added_Cost_And_Space(int16_t, int16_t* cost, int16_t* space) {
        *cost = 2;
        *space = 5;
    }
} // namespace DESBOX

namespace JIM {
    // The native loop's music hold (native.cpp Tick): no sound in the tests.
    void __cdecl Play_Streaming_Music(int16_t, int16_t, int16_t) {}
} // namespace JIM

// Auto Design (auto_design / design_auto, 2026-11-20): counted, the design given one weapon.
namespace TEST_DESIGN {
    int auto_designs = 0;
} // namespace TEST_DESIGN

namespace INITSHIP {
    void __cdecl Init_Ship_Designs(int16_t) {
        ++TEST_DESIGN::auto_designs;
    }
    void __cdecl Auto_Strategic_Design_Ship(int16_t, int16_t design_idx, s_ship_design* design) {
        ++TEST_DESIGN::auto_designs;
        design->size = (int8_t)design_idx;
    }
} // namespace INITSHIP

namespace struct_ {
    void __cdecl Clear_Structure(void* data, uint32_t size) {
        memset(data, 0, size);
    }
} // namespace struct_

namespace MOX {
    int16_t cur_combat_x = 0;
} // namespace MOX

namespace aidesign {
    void Auto_Design_Ship(const game::s_view*, game::id_player, s_ship_design* design, int16_t hull_idx) {
        ++TEST_DESIGN::auto_designs;
        design->size = (int8_t)hull_idx;
        design->ship_weapon[0].type = 1;
        design->ship_weapon[0].count = 1;
    }
} // namespace aidesign

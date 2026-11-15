#include "pch.h"

// Stand-ins for what src/antares/planet_queries.cpp calls in the game (WP3.5b), for
// tests/unit/planet_queries_test.cpp: the Planets screen's helpers with their results set by
// the test, and its send / recall recorded.

namespace TEST_PLANETS {
    bool visited[8] = {true, true, true, true, true, true, true, true};
    bool out_of_range[8] = {};
    bool free_colony = true;
    bool free_outpost = false;
    bool flux = false;
    int16_t best_eta = 1003; // a moving ship's ETA (the screen adds 1000)
    int16_t ship_to_send = 5;
    int8_t en_route_colony[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int sends = 0;
    int recalls = 0;
    int warnings = 0;
} // namespace TEST_PLANETS

namespace MOX {
    s_scroll_bar* scroll_bar = nullptr;
} // namespace MOX

namespace COLCALC {
    uint8_t minerals_per_mine[8] = {1, 2, 3, 5, 8};
} // namespace COLCALC

namespace EVENTS {
    int16_t __cdecl Event_Check_Hyperspace_Flux() {
        return TEST_PLANETS::flux ? 1 : 0;
    }
} // namespace EVENTS

namespace GENDRAW {
    void __cdecl Warning_Box(const char*) {
        ++TEST_PLANETS::warnings;
    }
} // namespace GENDRAW

namespace HACCESS {
    int8_t __cdecl Special_Is_Planet_Special(int16_t) {
        return 0;
    }
} // namespace HACCESS

namespace HAROLD {
    int16_t __cdecl First_Ship_Orbiting_Star(s_star_data*) {
        return -1;
    }
    bool __cdecl Star_Guarded_By_Monster(int16_t, int16_t*) {
        return false;
    }
} // namespace HAROLD

namespace PLNTSUM {
    s_plntsum_screen* screen = nullptr;
    s_plntsum_planet* planet_summary = nullptr;
    s_plntsum_ship* ship_summary = nullptr;
    int16_t n_ships_in_summary = 0;
    int16_t send_choice = -1;
    uint8_t found_colony_ship = 0;
    uint8_t found_outpost_ship = 0;
    int16_t last_ship_moved = -1;

    bool __cdecl Planet_Has_Players_Colony(int16_t p) {
        const int16_t c = MOX::planet[p].colony_index;
        return c != -1 && MOX::colony[c].owner == MOX::PLAYER_NUM;
    }
    bool __cdecl Planet_Is_Outpost_Planet(int16_t p) {
        const int16_t c = MOX::planet[p].colony_index;
        return c != -1 && MOX::colony[c].outpost_flag != 0;
    }
    bool __cdecl Planet_Has_Been_Visited(int16_t p) {
        return TEST_PLANETS::visited[p];
    }
    bool __cdecl Planet_In_Range_Of_A_Colony(int16_t p) {
        return !TEST_PLANETS::out_of_range[p];
    }
    int16_t __cdecl Productivity_Penalty(int16_t, int16_t gravity) {
        return gravity == PLANET_GRAVITY_HEAVY_G ? 50 : 0;
    }
    bool __cdecl Planet_Has_Adverse_Gravity(int16_t p, int16_t player) {
        return Productivity_Penalty(player, MOX::planet[p].gravity_class) > 0;
    }
    bool __cdecl Planet_Is_Enemy_Controlled(int16_t p, int16_t) {
        const int16_t c = MOX::planet[p].colony_index;
        return c != -1 && MOX::colony[c].owner != MOX::PLAYER_NUM;
    }
    bool __cdecl Planet_Has_Hostile_Environment(int16_t p) {
        return MOX::planet[p].food_per_farmer <= 0;
    }
    bool __cdecl Planet_Is_Mineral_Scarce(int16_t p) {
        return MOX::planet[p].mineral_class == MINERAL_RESOURCE_POOR || MOX::planet[p].mineral_class == MINERAL_RESOURCE_ULTRA_POOR;
    }
    void __cdecl Update_Ship_Etas() {
        for (int16_t i = 0; i < MOX::scroll_bar->total_rows; ++i) {
            planet_summary[i].colony_eta = TEST_PLANETS::en_route_colony[planet_summary[i].explored];
            planet_summary[i].outpost_eta = -1;
        }
    }
    void Build_Ship_Summary() {
        found_colony_ship = TEST_PLANETS::free_colony ? 1 : 0;
        found_outpost_ship = TEST_PLANETS::free_outpost ? 1 : 0;
        Update_Ship_Etas();
    }
    void __cdecl Reset_Ship_To_Star_Distances() {
        for (int16_t i = 0; i < MOX::scroll_bar->total_rows; ++i) {
            (send_choice == 0 ? planet_summary[i].best_colony_eta : planet_summary[i].best_outpost_eta) = TEST_PLANETS::best_eta;
        }
    }
    // The questions answered Yes: the ship goes and is tagged for the planet.
    void __cdecl Send_Nearest_Ship(int16_t planet_idx) {
        ++TEST_PLANETS::sends;
        last_ship_moved = TEST_PLANETS::ship_to_send;
        TEST_PLANETS::en_route_colony[planet_idx] = 3;
    }
    void __cdecl Undo_Send_Order(int16_t planet_idx, int16_t) {
        ++TEST_PLANETS::recalls;
        TEST_PLANETS::en_route_colony[planet_idx] = -1;
    }
} // namespace PLNTSUM

#include "pch.h"

#include <string>
#include <vector>

// Stand-ins for what src/antares/colony_queries.cpp calls in the game (WP3.4), for
// tests/unit/colony_queries_test.cpp. Costs, turns and buying are simple and recorded.

namespace TEST_COLONY {
    int32_t cost = 500;
    int16_t turns = 4;
    int32_t buy_cost = 1200;
    int colony_calculations = 0;
    int buys = 0;
    bool refuse_job = false; // Give_Colonist_New_Job says no (as its help message would)
} // namespace TEST_COLONY

namespace COLCALC {
    int32_t __cdecl Colony_Producing_Product_Cost(s_colony*) {
        return TEST_COLONY::cost;
    }
    int16_t __cdecl Colony_N_Turns_To_Produce(s_colony*) {
        return TEST_COLONY::turns;
    }
    int32_t __cdecl Colony_Cost_To_Buy_Producing_Product(s_colony*) {
        return TEST_COLONY::buy_cost;
    }
    void __cdecl Colony_Calculation(int16_t) {
        ++TEST_COLONY::colony_calculations;
    }
} // namespace COLCALC

namespace COLONY {
    int16_t __cdecl Pop_To_Pop_State(int16_t colony_idx, int16_t pop_idx) {
        const uint32_t race = MOX::colony[colony_idx].pop[pop_idx] & 0xF;
        return race == 9 ? 3 : (race == 8 ? 4 : 2);
    }
    int16_t __cdecl Get_Effective_Pop_Player(int16_t colony_idx, int16_t pop_idx) {
        const uint32_t race = MOX::colony[colony_idx].pop[pop_idx] & 0xF;
        return race >= 8 ? (int16_t)MOX::colony[colony_idx].owner : (int16_t)race;
    }
    int16_t __cdecl Satellite_Anim_Pic(int16_t id) {
        switch (id) {
            case BUILDING_ARTEMIS_SYSTEM_NET: return 7;
            case BUILDING_BATTLESTATION: return 1;
            case BUILDING_DIMENSIONAL_PORTAL: return 3;
            case BUILDING_STAR_BASE: return 2;
            case BUILDING_STAR_FORTRESS: return 0;
            default: return -1;
        }
    }
    void __cdecl Buy_Or_Unbuy_Product(int16_t colony_idx, bool) {
        ++TEST_COLONY::buys;
        MOX::colony[colony_idx].bought_outright = MOX::colony[colony_idx].bought_outright != 0 ? 0 : 100;
    }
} // namespace COLONY

namespace COLMOVE {
    int16_t __cdecl Get_Num_Unemployed(int16_t) {
        return 0;
    }
    uint8_t __cdecl Give_Colonist_New_Job(int16_t colony_idx, int16_t pop_idx, int16_t new_job, bool) {
        if (TEST_COLONY::refuse_job) {
            return 0;
        }
        uint32_t* pop = &MOX::colony[colony_idx].pop[pop_idx];
        POP::Set_Prof(pop, (POP::e_prof)new_job);
        POP::Set_Assigned(pop, true);
        return 1;
    }
} // namespace COLMOVE

namespace COLBLDG {
    char* __cdecl Option_String(int16_t option_id) {
        static char text[32];
        snprintf(text, sizeof(text), "Option %d", option_id);
        return text;
    }
} // namespace COLBLDG

namespace ESTRINGS {
    char* __cdecl E_Strings(int16_t) {
        static char text[] = "%s %s";
        return text;
    }
} // namespace ESTRINGS

// ---- The build pane (WP3.4b): a small working queue, as the popup's functions keep it ----

namespace TEST_COLONY {
    std::vector<std::string> build_calls; // what the commit called, in order
} // namespace TEST_COLONY

namespace COLONY {
    int16_t colony_handle = -1;
    int16_t planet_handle = -1;
    int16_t star_handle = -1;
    int16_t __cdecl Calculate_Colony_Turn_Count_From_Scrap_For_Prod(int16_t, int16_t) {
        return 7;
    }
} // namespace COLONY

namespace COLCALC {
    int32_t __cdecl Colony_Product_Cost(s_colony*, int16_t id) {
        return id > 0 ? 100 * id : 250;
    }
} // namespace COLCALC

namespace COLBLDG {
    bool quiet_ui = false;
    uint8_t field_mode;
    int16_t active_prod[4] = {-1, 0, 0, 0};
    int16_t current_item[7];
    int16_t colony_auto_building;
    int16_t building_indexes[54];
    int16_t n_building_indexes;
    int16_t military_indexes[54];
    int16_t n_military_indexes;

    void __cdecl Build_Arrays() {
        building_indexes[0] = COLONY_PRODUCTION_TRADE_GOODS;
        building_indexes[1] = BUILDING_MARINE_BARRACKS;
        n_building_indexes = 2;
        military_indexes[0] = COLONY_PRODUCTION_FREIGHTERS;
        military_indexes[1] = COLONY_PRODUCTION_SEPARATOR;
        military_indexes[2] = COLBLDG::Colony_Production_From_Ship_Design_Index(0);
        n_military_indexes = 3;
    }
    int8_t __cdecl Add_To_Queue(int16_t id) {
        TEST_COLONY::build_calls.push_back("add " + std::to_string(id) + (field_mode == 2 ? " repeat" : "") + (quiet_ui ? " quiet" : ""));
        for (int i = 0; i < 7; ++i) {
            if (current_item[i] == COLONY_PRODUCTION_NONE || current_item[i] == 0) {
                current_item[i] = id;
                return 1;
            }
        }
        return 0;
    }
    void __cdecl Delete_From_Queue(int16_t slot) {
        TEST_COLONY::build_calls.push_back("delete " + std::to_string(slot));
        for (int i = slot; i < 6; ++i) {
            current_item[i] = current_item[i + 1];
        }
        current_item[6] = COLONY_PRODUCTION_NONE;
    }
    void __cdecl Insert_Before_Queue_Slot(int16_t from, int16_t to) {
        TEST_COLONY::build_calls.push_back("move " + std::to_string(from) + " " + std::to_string(to));
        std::swap(current_item[from], current_item[to]);
    }
    void __cdecl Defragment_Queue() {}
    void Commit_Build_Queue() {
        TEST_COLONY::build_calls.push_back(std::string("commit") + (quiet_ui ? " quiet" : ""));
        for (int i = 0; i < 7; ++i) {
            MOX::colony[COLONY::colony_handle].producing[i] = current_item[i];
        }
        MOX::colony[COLONY::colony_handle].auto_building = (uint8_t)colony_auto_building;
    }
    void __cdecl Build_Weapon_String(s_ship_weapons*, char* dest, uint32_t size, uint8_t, uint8_t) {
        snprintf(dest, size, "Laser");
    }
} // namespace COLBLDG

namespace struct_ {
    int16_t __cdecl Test_Bit_Field(void* bits, uint16_t index) {
        return (static_cast<uint8_t*>(bits)[index / 8] >> (index % 8)) & 1;
    }
} // namespace struct_

namespace TECHDATA {
    s_tech_shield_data shields[SHIELD_COUNT];
    s_tech_computer_data computers[COMPUTER_COUNT];
    s_tech_armor_data armor[ARMOR_COUNT];
    s_tech_drive_data drives[DRIVE_COUNT];
    s_tech_specials_data specials[SPECIAL_COUNT];
} // namespace TECHDATA

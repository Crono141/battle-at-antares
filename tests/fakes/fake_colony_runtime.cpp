#include "pch.h"

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

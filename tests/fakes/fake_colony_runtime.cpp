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
    int refuse_after = -1;   // Give_Colonist_New_Job says no after this many moves (-1: never)
    int sells = 0;
    bool answer_sell = true; // the game's question
    bool captured_people = false; // Colony_Pop_Is_Annihilation_Target for every pop
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
    int16_t orbit_temp = 0;
    bool __cdecl Colony_Pop_Is_Annihilation_Target(const s_colony*, uint32_t) {
        return TEST_COLONY::captured_people;
    }
    void __cdecl Col_Calc_Wrapper(int16_t) {
        ++TEST_COLONY::colony_calculations;
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
        if (TEST_COLONY::refuse_job || TEST_COLONY::refuse_after == 0) {
            return 0;
        }
        if (TEST_COLONY::refuse_after > 0) {
            --TEST_COLONY::refuse_after;
        }
        uint32_t* pop = &MOX::colony[colony_idx].pop[pop_idx];
        POP::Set_Prof(pop, (POP::e_prof)new_job);
        POP::Set_Assigned(pop, true);
        return 1;
    }
    // Same job, race and state bits, as the game compares them.
    bool __cdecl Pops_Identical(int16_t source_colony_idx, int16_t source_pop_idx, int16_t dest_colony_idx, int16_t dest_pop_idx) {
        const uint32_t a = MOX::colony[source_colony_idx].pop[source_pop_idx];
        const uint32_t b = MOX::colony[dest_colony_idx].pop[dest_pop_idx];
        return (a & 0x7F) == (b & 0x7F) && ((a >> 7) & 3) == ((b >> 7) & 3) && (a & 0x400) == (b & 0x400);
    }
} // namespace COLMOVE

namespace COLONY {
    bool landscape_only = false;

    bool Building_Can_Be_Scrapped(int16_t bldg_id) {
        return bldg_id > 0 && bldg_id != BUILDING_STAR_BASE && bldg_id != BUILDING_ARTEMIS_SYSTEM_NET;
    }
    int32_t Building_Scrap_Refund(int16_t bldg_id) {
        return TECHDATA::buildings[bldg_id].cost / 2;
    }
    // As the game's: the question, then the building goes and half its cost comes back.
    bool Scrap_Building(int16_t colony_idx, int16_t bldg_id, bool) {
        ++TEST_COLONY::sells;
        if (!TEST_COLONY::answer_sell) {
            return false;
        }
        MOX::colony[colony_idx].buildings[bldg_id] = 0;
        MOX::player[MOX::colony[colony_idx].owner].bc += Building_Scrap_Refund(bldg_id);
        return true;
    }
} // namespace COLONY

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
    bool Restore_Refit_Original(int16_t ship_idx);

    void __cdecl Delete_From_Queue(int16_t slot) {
        TEST_COLONY::build_calls.push_back("delete " + std::to_string(slot));
        if (Colony_Production_Is_Queued_Ship(current_item[slot])) {
            Restore_Refit_Original(Colony_Production_Ship_Index(current_item[slot]));
        }
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

namespace COLBLDG {
    namespace {
        int16_t g_refit_ship = -1;
        s_ship_design g_refit_design;
        std::vector<std::pair<int16_t, s_ship_data>> g_originals;
    } // namespace

    // As the game's (Antares fix): refits of this session are remembered and put back.
    void Forget_Refit_Originals() {
        g_originals.clear();
    }

    bool Has_Refit_Original(int16_t ship_idx) {
        for (auto& o : g_originals) {
            if (o.first == ship_idx) {
                return true;
            }
        }
        return false;
    }

    bool Restore_Refit_Original(int16_t ship_idx) {
        for (auto it = g_originals.begin(); it != g_originals.end(); ++it) {
            if (it->first == ship_idx) {
                MOX::ship[ship_idx] = it->second;
                g_originals.erase(it);
                return true;
            }
        }
        return false;
    }

    char* __cdecl Selection_Name(int16_t id) {
        static char name[64];
        if (Colony_Production_Is_Ship_Design(id)) {
            strlcpy(name, MOX::player[MOX::PLAYER_NUM].ship_designs[Colony_Production_Ship_Design_Index(id)].name, sizeof(name));
        } else {
            name[0] = '\0';
        }
        return name;
    }

    void Set_Pending_Refit_Design(int16_t ship_idx, const s_ship_design* design) {
        g_refit_ship = ship_idx;
        g_refit_design = *design;
    }

    // As the game's: queue the ship, then give it the new design and the refit's cost.
    void __cdecl Add_Refited_Ship_To_Queue() {
        for (int16_t& slot : current_item) {
            if (slot == COLONY_PRODUCTION_NONE || slot == 0) {
                slot = Colony_Production_From_Ship_Index(g_refit_ship);
                g_originals.emplace_back(g_refit_ship, MOX::ship[g_refit_ship]);
                MOX::ship[g_refit_ship].d = g_refit_design;
                MOX::ship[g_refit_ship].status = 4;
                MOX::ship[g_refit_ship].d.cost = 25;
                return;
            }
        }
    }
} // namespace COLBLDG

namespace AIBUILD {
    int16_t __cdecl Refit_Cost(const s_player*, const s_ship_design*, const s_ship_design*) {
        return 25;
    }
} // namespace AIBUILD

namespace DESIGN {
    int16_t __cdecl Cost_Of_Design(int16_t, s_ship_design* design) {
        return design->cost;
    }
} // namespace DESIGN

namespace TEXTBOX {
    void __cdecl Get_Help_Lbx_Name(char* name, size_t size) {
        strlcpy(name, "HELP.LBX", size);
    }
} // namespace TEXTBOX

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

// ---- The Colonies panel (WP3.5a): the list's helpers and the settlers' move ----

namespace TEST_COLONY {
    int16_t event_colony = -1; // the colony with an event (Colony_Has_Event)
    int send_calls = 0;
} // namespace TEST_COLONY

namespace COLSUM {
    char* wee_event_strings[36] = {};
    static char s_test_event_[] = "Plague";
    void __cdecl Load_Colsum_Strings() {
        wee_event_strings[3] = s_test_event_;
    }
    bool __cdecl Event_Good(int16_t event_id) {
        return event_id != 3;
    }
    // By what is built, then the colony's number (the game's: the item's kind, name and turns).
    int16_t __cdecl cmp_Prod(int16_t a, int16_t b) {
        const int16_t pa = MOX::colony[a].producing[0];
        const int16_t pb = MOX::colony[b].producing[0];
        return pa != pb ? (pa < pb ? -1 : 1) : (a < b ? -1 : (a > b ? 1 : 0));
    }
} // namespace COLSUM

namespace EVENTS {
    int16_t __cdecl Colony_Has_Event(int16_t colony_idx) {
        return colony_idx == TEST_COLONY::event_colony ? 3 : -1;
    }
} // namespace EVENTS

namespace COLONY {
    int16_t __cdecl Calculate_Current_Production_Turn_Count(int16_t) {
        return TEST_COLONY::turns;
    }
    bool __cdecl Colony_Can_Buy_Product_0(int16_t colony_idx) {
        return MOX::colony[colony_idx].bought_outright == 0;
    }
} // namespace COLONY

namespace COLMOVE {
    int16_t cluster_colony_n = -1;
    // Picked up: the colonist and the identical ones after it lose their assignment.
    void __cdecl Get_Cluster(int16_t colony_idx, int16_t start_pop_idx) {
        cluster_colony_n = colony_idx;
        s_colony& colony = MOX::colony[colony_idx];
        for (int16_t i = start_pop_idx; i < (uint8_t)colony.n_pops; ++i) {
            if (Pops_Identical(colony_idx, start_pop_idx, colony_idx, i)) {
                POP::Set_Assigned(&colony.pop[i], false);
            }
        }
    }
    void __cdecl Clear_Cluster() {
        s_colony& colony = MOX::colony[cluster_colony_n];
        for (int16_t i = 0; i < (uint8_t)colony.n_pops; ++i) {
            POP::Set_Assigned(&colony.pop[i], true);
        }
        cluster_colony_n = -1;
    }
    // To another colony: the picked-up colonists leave the source (here they arrive at once).
    void __cdecl Send_Cluster(int16_t colony_idx, int16_t requested_job) {
        ++TEST_COLONY::send_calls;
        if (colony_idx == cluster_colony_n) {
            return; // (the panel uses move_colonist within a colony)
        }
        s_colony& from = MOX::colony[cluster_colony_n];
        s_colony& to = MOX::colony[colony_idx];
        int16_t kept = 0;
        for (int16_t i = 0; i < (uint8_t)from.n_pops; ++i) {
            uint32_t pop = from.pop[i];
            if ((pop & 0x200) != 0) {
                from.pop[kept++] = pop;
                continue;
            }
            POP::Set_Assigned(&pop, true);
            POP::Set_Prof(&pop, (POP::e_prof)(requested_job < 0 ? 0 : requested_job));
            to.pop[(uint8_t)to.n_pops] = pop;
            to.n_pops = (uint8_t)to.n_pops + 1;
        }
        from.n_pops = kept;
        cluster_colony_n = -1;
    }
} // namespace COLMOVE

// The colony pane's transport list (transport_targets, 2026-11-20): every destination can take
// them, one turn per star index apart.
namespace SETTLER {
    void __cdecl Pop_Tries_To_Settle(s_colony*, int16_t, s_colony*, s_settler_settle_result* result) {
        *result = {};
        result->can_settle = 1;
        result->eta_turns = 1;
    }
    int32_t __cdecl Settler_ETA(s_player*, int16_t source_star, int16_t dest_star) {
        return source_star > dest_star ? source_star - dest_star : dest_star - source_star;
    }
} // namespace SETTLER

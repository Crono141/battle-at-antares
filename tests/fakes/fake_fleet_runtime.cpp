#include "pch.h"

#include <set>
#include <vector>

// Stand-ins for what src/antares/fleet_orders.cpp calls in the game, for
// tests/unit/fleet_orders_test.cpp. The globals are real; the rules functions answer from
// TEST_FLEET, which the tests set, and record what the commands asked for.

namespace TEST_FLEET {
    std::set<int16_t> not_orderable;   // ships Player_Can_Order_Ship refuses
    s_ship_move_info try_result{};     // what Ships_Try_To_Move_To answers
    bool apply_answer = true;          // Apply_Player_Movement_Order: false = the player said No
    std::vector<int16_t> tried;        // the last list Ships_Try_To_Move_To got (without the -1)
    std::vector<int16_t> applied;      // the last list Apply_Player_Movement_Order got
    int16_t applied_star = -1;
    bool colonize_flag = false;        // the button flags
    bool outpost_flag = false;
    bool troops_flag = false;
    bool engage_flag = false;
    bool antares_flag = false;
    int16_t flag_stack = -1;           // the stack the last flag function got
    std::vector<int16_t> colonies;     // Make_New_Colony's planets
    std::vector<int16_t> outposts;     // Make_New_Outpost's planets
    std::vector<int16_t> pooled;       // Move_Officer_To_Pool's officers
    std::vector<int16_t> deployed;     // Deploy_Ships_To_Attack_Antares's list
    int16_t user_box_answer = 1;
    std::vector<int16_t> user_boxes;   // User_Box types asked
    int box_data_calls = 0;
    int vanished = 0;
    int relocations = 2;               // N_Star_Relocations
    int16_t relocated_to = -1;
} // namespace TEST_FLEET

namespace MOX {
    uint8_t first_time;
    int16_t fleet_ship_scanned;
    int16_t first_node_ptr_in_stack_;
    int8_t just_moved_stack;
    s_leader_data* leaders;
    int16_t ship_stack_count;
    int16_t ship_stack_start[MAX_SHIPS];
    int16_t fleet_box_ship_stack = -1;
} // namespace MOX

namespace {
    std::vector<int16_t> List_(const int16_t* list) {
        std::vector<int16_t> out;
        for (int i = 0; list[i] != -1; ++i) {
            out.push_back(list[i]);
        }
        return out;
    }
} // namespace

namespace SHIPMOVE {
    void __cdecl Ships_Try_To_Move_To(int16_t* ship_list, int16_t, s_ship_move_info* move_info) {
        TEST_FLEET::tried = List_(ship_list);
        *move_info = TEST_FLEET::try_result;
    }

    bool __cdecl Apply_Player_Movement_Order(int16_t* ship_list, int16_t star, s_ship_move_info*) {
        TEST_FLEET::applied = List_(ship_list);
        TEST_FLEET::applied_star = star;
        return TEST_FLEET::apply_answer;
    }
} // namespace SHIPMOVE

namespace FLEETPOP {
    void __cdecl Clear_Ship_Node_Selection_Flags() {}
    void __cdecl Fleet_Box_Data(int16_t) {
        ++TEST_FLEET::box_data_calls;
    }
    void __cdecl Get_Absolute_Location_Scan_Info(int16_t, int16_t* value, int16_t* star, int16_t* type) {
        *value = 1;
        *star = 0;
        *type = 2;
    }
    int16_t __cdecl N_Fleet_Icons(int16_t) {
        return 1;
    }
    bool __cdecl Set_Attack_Antares_Button_Flag(int16_t stack) {
        TEST_FLEET::flag_stack = stack;
        return TEST_FLEET::antares_flag;
    }
    uint8_t __cdecl Set_Build_Outpost_Button_Flag(int16_t stack, int16_t) {
        TEST_FLEET::flag_stack = stack;
        return TEST_FLEET::outpost_flag;
    }
    int8_t __cdecl Set_Colonize_Button_Flag(int16_t stack, int16_t) {
        TEST_FLEET::flag_stack = stack;
        return TEST_FLEET::colonize_flag;
    }
    int8_t __cdecl Set_Engage_Enemy_Forces_Flag(int16_t stack, int16_t) {
        TEST_FLEET::flag_stack = stack;
        return TEST_FLEET::engage_flag;
    }
    int8_t __cdecl Set_Transport_Troops_Flag(int16_t stack, int16_t) {
        TEST_FLEET::flag_stack = stack;
        return TEST_FLEET::troops_flag;
    }
} // namespace FLEETPOP

namespace HACCESS {
    void __cdecl Clear_Fleet_Box_Selected_Ship_Status() {}
    char* __cdecl Get_Planet_Name(int16_t) {
        static char name[] = "Planet";
        return name;
    }
    bool __cdecl Is_Combat_Ship(int16_t ship) {
        return MOX::ship[ship].d.ship_type == SHIP_TYPE_COMBAT;
    }
    int16_t __cdecl N_Star_Relocations(int16_t) {
        return (int16_t)TEST_FLEET::relocations;
    }
    bool __cdecl Player_Can_Order_Ship(int16_t, s_ship_data* ship) {
        return TEST_FLEET::not_orderable.count((int16_t)(ship - MOX::ship)) == 0;
    }
    void __cdecl Race_Name(int16_t, char* out, uint32_t size) {
        strlcpy(out, "Race", size);
    }
    void __cdecl Set_All_Star_Relocations(int16_t star, int16_t) {
        TEST_FLEET::relocated_to = star;
    }
    int16_t __cdecl Ship_Stack_Star_Id(int16_t node) {
        return MOX::ship[MOX::ship_node[node].ship_idx].location;
    }
} // namespace HACCESS

namespace HAROLD {
    int16_t __cdecl User_Box(uint8_t*, const char*, int16_t type) {
        TEST_FLEET::user_boxes.push_back(type);
        return TEST_FLEET::user_box_answer;
    }
} // namespace HAROLD

namespace COLONIZE {
    void __cdecl Make_New_Colony(int16_t planet, int16_t) {
        TEST_FLEET::colonies.push_back(planet);
    }
    void __cdecl Make_New_Outpost(int16_t planet, int16_t) {
        TEST_FLEET::outposts.push_back(planet);
    }
} // namespace COLONIZE

namespace COLCALC {
    void __cdecl Update_Player_Stats(s_player*) {}
} // namespace COLCALC

namespace invasion {
    int __cdecl Unload_Transports(s_colony*, int16_t*, int count) {
        return count;
    }
} // namespace invasion

namespace MAINSCR {
    void __cdecl Update_Ship_Icons_In_Between_Turns() {}
} // namespace MAINSCR

namespace MISC {
    bool __cdecl Occluded(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t) {
        return false;
    }
} // namespace MISC

namespace MOVEBOX {
    int16_t __cdecl Moveable_Box_Selected(int16_t box) {
        return MOX::moveable_box[box].id > 0;
    }
    void __cdecl Vanish_Fleet_Movement_Box() {
        ++TEST_FLEET::vanished;
        MOX::moveable_box[2].id = -1;
    }
} // namespace MOVEBOX

namespace OFFICER {
    void __cdecl Move_Officer_To_Pool(int16_t leader) {
        TEST_FLEET::pooled.push_back(leader);
        MOX::leaders[leader].location = -1;
        MOX::leaders[leader].status = 0;
    }
} // namespace OFFICER

namespace SHIPS {
    void __cdecl Deploy_Ships_To_Attack_Antares(int16_t* list) {
        TEST_FLEET::deployed = List_(list);
    }
    void __cdecl Get_Ship_Icon_Coords(int16_t, int16_t* x, int16_t* y) {
        *x = 0;
        *y = 0;
    }
} // namespace SHIPS

namespace strings {
    void __cdecl String_To_Upper(char*) {}
} // namespace strings

// WP3.3d: the empire query's side bar figures and set_tax_rate.
namespace TEST_EMPIRE {
    int16_t turns = 3;   // Player_N_Turns_Until_Research_Complete
    int32_t chance = 12; // Chance_For_Research_Breakthrough
    int recomputed = 0;  // Do_Player_Colony_Calculations calls
} // namespace TEST_EMPIRE

namespace COLCALC {
    int16_t __cdecl Player_N_Turns_Until_Research_Complete(s_player*) {
        return TEST_EMPIRE::turns;
    }
    int32_t __cdecl Chance_For_Research_Breakthrough(s_player*) {
        return TEST_EMPIRE::chance;
    }
    void __cdecl Do_Player_Colony_Calculations(int16_t) {
        ++TEST_EMPIRE::recomputed;
    }
} // namespace COLCALC

namespace MAINSCR {
    void __cdecl Get_Research_Unit_String(char* out) {
        strlcpy(out, "RP", 16);
    }
} // namespace MAINSCR

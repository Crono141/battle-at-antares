#include "pch.h"

#include "antares/sprites.h"

#include <set>
#include <vector>

// Stand-ins for what src/antares/galaxy_queries.cpp calls in the game, for
// tests/unit/native_test.cpp. The globals are real; the rules helpers answer from TEST_GALAXY, which
// the tests set, and record what the commands did.

namespace TEST_GALAXY {
    std::set<int16_t> visited;  // stars the player has visited
    std::set<int16_t> viewable; // stars SYS::Ok_To_View_System allows
    int16_t last_colony = -1;   // what HACCESS::Last_Colony_Selected answers
    int last_colony_calls = 0;
    std::vector<int16_t> colony_screens; // Do_Colony_Screen's colonies
    std::vector<int16_t> toggles;        // Toggle_Game_Option's options
    int fleet_deselects = 0;
    int box_to_top = 0;
    int box_vanished = 0;
    int16_t cycle_to = -1; // where Cycle_Star_Icons moves
} // namespace TEST_GALAXY

namespace MOX {
    s_star_data* star;
    s_planet_data* planet;
    s_colony* colony;
    s_nebula* nebula;
    uint8_t NUM_NEBULAS;
    int16_t NUM_COLONIES;
    int16_t planet_count;
    int16_t cur_map_scale;
    int16_t cur_map_x;
    int16_t cur_map_y;
    int16_t max_map_scale;
    int16_t MAP_MAX_X;
    int16_t MAP_MAX_Y;
    s_settings settings;
    s_ship_icon ship_icon[SHIP_ICON_COUNT];
    int16_t ship_icon_count;
    s_ship_node ship_node[MAX_SHIPS];
    uint16_t star_fields_dim[6] = {33, 29, 25, 23, 21, 17};
    uint8_t rot_plan_dim[PLANET_SIZE_COUNT] = {19, 20, 22, 22, 24};
    int16_t system_star_id_for_auto_select = -1;
    s_movable_box moveable_box[5];
    char* mineral_class_string[MINERAL_RESOURCE_COUNT];
    char* planet_climate_string[PLANET_CLIMATE_COUNT];
    char* planet_gravity_string[PLANET_GRAVITY_COUNT];
    char* planet_size_string[PLANET_SIZE_COUNT];
    char* planet_special_string[SYSTEM_SPECIAL_COUNT];
    s_skill_data skill_data[54];
    int16_t first_field_in_group[10] = {0, 18, 55, 57, 29, 7, 22, 28, 10, 74};
} // namespace MOX

namespace TEST_GALAXY {
    std::set<int> leader_skills; // the skills every leader has (WP3.3f: the governor's skills)
} // namespace TEST_GALAXY

namespace TEST_GALAXY {
    int stream_volume = -1; // the last music volume set_volume gave the sound system
} // namespace TEST_GALAXY

namespace sound {
    void __cdecl Stop_Stream() {}
    void __cdecl Stream_On() {}
    void __cdecl Set_Stream_Max_Volume(int32_t) {}
    void __cdecl Set_Stream_Volume(int32_t volume) {
        TEST_GALAXY::stream_volume = volume;
    }
    void __cdecl Sound_On() {}
    void __cdecl Set_Sound_Max_Volume(int32_t) {}
} // namespace sound

namespace JIM {
    void Play_Background_Music() {}
    void __cdecl Stop_Background_Music() {}
    void __cdecl Get_Text_Message(const char*, int16_t, char* dest, int16_t max_len) {
        snprintf(dest, (size_t)max_len, "No applications");
    }
} // namespace JIM

namespace TECH {
    char* __cdecl Technology_Applications_Name(int16_t app, int16_t) {
        static char names[8][16];
        static int next = 0;
        char* name = names[next++ % 8];
        snprintf(name, sizeof(names[0]), "App %d", app);
        return name;
    }
    char* __cdecl Technology_Fields_Name(int16_t field) {
        static char name[16];
        snprintf(name, sizeof(name), "Field %d", field);
        return name;
    }
    int16_t __cdecl Get_Hyper_Tech_App_ID(int16_t field) {
        return (int16_t)(200 + field - TECH_FIELD_BIOLOGY);
    }
} // namespace TECH

namespace OFFICER {
    bool __cdecl Officer_Has_Special_Skill(int16_t, int16_t, int16_t skill_idx) {
        return TEST_GALAXY::leader_skills.count(skill_idx) != 0;
    }
    bool __cdecl Officer_Has_General_Skill(int16_t, int16_t, int16_t skill_idx) {
        return TEST_GALAXY::leader_skills.count(skill_idx) != 0;
    }
    int32_t __cdecl Officer_Skill_Bonus(uint32_t, int16_t, int16_t skill_idx) {
        return 10 + skill_idx;
    }
} // namespace OFFICER

namespace TECHDATA {
    s_tech_building_data buildings[BUILDING_COUNT];
    s_tech_hull_data hull_data[HULL_CLASS_COUNT];
    s_tech_fuel_data fuel[FUEL_COUNT];
    s_tech_field_data technology_fields[TECH_FIELD_COUNT];
} // namespace TECHDATA

namespace HAROLD {
    const char* __cdecl H_Message(int16_t, int, const s_save_game_description*) {
        return "message"; // no format codes: some callers pass numbers
    }
    int16_t __cdecl Absolute_Location(int16_t location) {
        return location >= SHIP_LOCATION_MOVING_OFFSET ? (int16_t)(location - SHIP_LOCATION_MOVING_OFFSET) : location;
    }
    void __cdecl Colony_Owners(int16_t, int16_t* counts, int16_t* total, bool) {
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            counts[i] = 0;
        }
        *total = 0;
    }
    char __cdecl Contact_With_One_Colony(int16_t) {
        return 0;
    }
    bool __cdecl Contact_With_Player(int16_t, int16_t) {
        return false;
    }
    int16_t __cdecl N_Colony_Owners(int16_t) {
        return 1;
    }
    bool __cdecl Player_Has_Artemis_Net(s_player*, s_star_data*) {
        return false;
    }
    bool __cdecl Player_Has_Dimensional_Portal(s_player*, s_star_data*) {
        return false;
    }
    bool __cdecl Player_Has_Colony(int16_t, int16_t) {
        return false;
    }
    uint8_t __cdecl Player_Has_Visited(int16_t, int16_t star) {
        return TEST_GALAXY::visited.count(star) != 0 ? 1 : 0;
    }
    bool __cdecl Player_Is_Omniscient(int16_t) {
        return false;
    }
    bool __cdecl Player_Sighted_Monster(int16_t, int16_t) {
        return false;
    }
    bool __cdecl Star_Has_Artemis_Net(s_star_data*) {
        return false;
    }
    bool __cdecl Star_Has_Dimensional_Portal(s_star_data*) {
        return false;
    }
    bool __cdecl Star_Is_Outpost_Star(int16_t) {
        return false;
    }
} // namespace HAROLD

namespace HACCESS {
    char* __cdecl Get_Planet_Name_No_Prime(int16_t) {
        static char name[] = "Planet";
        return name;
    }
    int16_t __cdecl Last_Colony_Selected(int16_t, int16_t) {
        ++TEST_GALAXY::last_colony_calls;
        return TEST_GALAXY::last_colony;
    }
    int16_t __cdecl Relocation(int16_t, int16_t) {
        return -1;
    }
    bool __cdecl Star_Has_Relocation(int16_t, int16_t) {
        return false;
    }
} // namespace HACCESS

namespace COLCALC {
    int __cdecl Player_Research_Cost(s_player*, int16_t field) {
        return 100 * field;
    }
    void __cdecl Compute_Potential_Colony_Info(s_player*, s_planet_data*, s_potential_colony_info* info) {
        *info = s_potential_colony_info{};
        info->industry = 3;
        info->research = 2;
    }
    void Format_Food2(uint16_t food, char* buffer, uint32_t size) {
        snprintf(buffer, size, "%u", (unsigned)food / 2);
    }
    int16_t __cdecl Planet_Max_Population_For_Player(s_planet_data* planet, int16_t) {
        return planet->max_population;
    }
} // namespace COLCALC

namespace FLEETPOP {
    void __cdecl Set_Fleet_Movement_Icon_Selections(int16_t, uint8_t) {
        ++TEST_GALAXY::fleet_deselects;
    }
} // namespace FLEETPOP

namespace GEO {
    void __cdecl Get_Orbit_Display_X_Y(int16_t, int32_t orbit, int32_t* x, int32_t* y, int32_t* frame) {
        *x = orbit * 10;
        *y = 0;
        *frame = 0;
    }
} // namespace GEO

namespace MAINSCR {
    void __cdecl Cycle_Star_Icons(int16_t* star, int16_t, int16_t) {
        *star = TEST_GALAXY::cycle_to;
    }
    // As the game: an own outpost shows a message, an own colony opens its screen.
    void __cdecl Do_Colony_Screen(int16_t colony, int16_t* flag) {
        TEST_GALAXY::colony_screens.push_back(colony);
        if (MOX::colony[colony].outpost_flag == 0) {
            MOX::current_screen = SCREEN_COLONY;
            *flag = 1;
        }
    }
    void __cdecl Set_Popup_Draw_XY(int16_t, int16_t star, int16_t* x, int16_t* y) {
        *x = (int16_t)(100 + star);
        *y = 22;
    }
    void __cdecl Toggle_Game_Option(int16_t option) {
        TEST_GALAXY::toggles.push_back(option);
        if (option == 6) {
            MOX::settings.auto_select_colony = MOX::settings.auto_select_colony == 0;
        }
    }
} // namespace MAINSCR

namespace MOVEBOX {
    void __cdecl Move_Box_To_Top(int16_t) {
        ++TEST_GALAXY::box_to_top;
    }
    void __cdecl Vanish_System_Display_Box() {
        ++TEST_GALAXY::box_vanished;
        MOX::moveable_box[0].id = -1;
    }
} // namespace MOVEBOX

namespace SHIPS {
    bool __cdecl Enemy_Ship_Heading_Toward_Our_Colony(int16_t) {
        return false;
    }
} // namespace SHIPS

namespace SYS {
    void __cdecl N_Ships_By_Type_In_Stack(int16_t, int16_t* total, int16_t* colony, int16_t* outposts, int16_t* transports, int16_t* sizes) {
        *total = 1;
        *colony = *outposts = *transports = 0;
        for (int i = 0; i <= SHIP_SIZE_DOOM_STAR; ++i) {
            sizes[i] = 0;
        }
    }
    bool __cdecl Ok_To_View_System(int16_t star, int16_t) {
        return TEST_GALAXY::viewable.count(star) != 0;
    }
} // namespace SYS

namespace antares::sprites {
    bool Export(const std::string& lbx, int32_t entry, s_sprite* out) {
        *out = s_sprite{};
        out->lbx = lbx;
        out->entry = entry;
        out->error = "no art in tests";
        return false;
    }
} // namespace antares::sprites

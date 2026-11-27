#include "pch.h"

#include "antares/host.h"
#include "antares/rules_trace.h"

#include <vector>

// Stand-ins for what src/antares/native.cpp calls in the game, for tests/unit/native_test.cpp.
// The galaxy, rules and host calls do nothing; the screen globals are real variables.

namespace MOX {
    int16_t current_screen;
    int16_t return_screen;
    int16_t screen_data;
    int16_t temp_star_handle;
    uint8_t first_design_screen_call;
    uint8_t refit_ship;
    uint16_t disallow_saves_code;
    int8_t officer_scrn_type;
    uint8_t auto_turn_exit;
    uint16_t global_exit_field;
    s_player* player;
    int16_t PLAYER_NUM;
    int16_t NUM_PLAYERS;
    int32_t stardate;
    int8_t game_type;
    uint8_t given_chance_to_rename_home_star[MAX_PLAYERS];
    int16_t first_fleet_movement_icon;
    int16_t eta_star_id;
    int16_t stencil_box_id;
    int16_t draw_main_screen_stencilled;
    int16_t animated_star;
    uint8_t draw_main_screen_to_back;
    uint8_t at_least_one_tactical_combat;
    uint8_t dont_draw_main_screen_to_back;
    uint16_t FULL_DRAW_SCREEN_FLAG;
    s_ship_data* ship;
    int16_t NUM_SHIPS;
    int16_t NUM_STARS;
} // namespace MOX

// What the scrap_ships command passed on (tests/unit/native_test.cpp reads it).
namespace TEST_NATIVE {
    std::vector<int16_t> scrapped;
    int8_t scrap_answer = 1;
    int icon_rebuilds = 0;
} // namespace TEST_NATIVE

namespace FLT1 {
    int8_t __cdecl Scrap_Ships(int16_t, int16_t* ship_indices, int16_t num_ships, int8_t) {
        TEST_NATIVE::scrapped.assign(ship_indices, ship_indices + num_ships);
        return TEST_NATIVE::scrap_answer;
    }
} // namespace FLT1

namespace SHIPS {
    void __cdecl Build_Ship_Icons() {
        ++TEST_NATIVE::icon_rebuilds;
    }
    void __cdecl Set_Ship_Icon_XYs() {}
} // namespace SHIPS

namespace maintain {
    int16_t __cdecl Ship_Scrap_Value(s_ship_data* ship) {
        return ship->d.cost;
    }
} // namespace maintain

namespace HAROLD {
    int16_t __cdecl XY_To_Star_Id(int16_t, int16_t) {
        return -1;
    }
} // namespace HAROLD

namespace TEST_NATIVE {
    uint32_t random_seed = 0; // the build pane's commit sets and restores it (WP3.4b)
    std::vector<uint32_t> seeds_set;
} // namespace TEST_NATIVE

namespace game_random {
    uint32_t __cdecl Get_Random_Seed() {
        return TEST_NATIVE::random_seed;
    }
    uint32_t __cdecl Set_Random_Seed(uint32_t seed) {
        const uint32_t previous = TEST_NATIVE::random_seed;
        TEST_NATIVE::random_seed = seed;
        TEST_NATIVE::seeds_set.push_back(seed);
        return previous;
    }
} // namespace game_random

namespace antares {
    const char* Get_Env(const char*) {
        return nullptr;
    }
    namespace rules_trace {
        uint64_t State_Hash(std::string*) {
            return 0;
        }
    } // namespace rules_trace
} // namespace antares

namespace MAINSCR {
    int8_t officer_screen_return_screen;
    int8_t clear_fleet_fields;
    int8_t next_turn_hit;
    int16_t __cdecl Get_Ship_Stack_For_Officers_Screen() {
        return -1;
    }
    int16_t __cdecl Get_Star_Id_For_Officers_Screen() {
        return -1;
    }
    void __cdecl Do_Next_Turn(int16_t* exit_flag, int8_t) {
        *exit_flag = 1;
    }
    void __cdecl Update_Star_Owners() {}
    void __cdecl Set_Reload_Fleet_Box_Palette_Flag() {}
    void __cdecl Clear_Main_Screen_Fields() {}
    void __cdecl Draw_Mini_Main_Screen() {}
} // namespace MAINSCR

namespace MAINSCR2 {
    uint8_t main_screen_fade_in_flag;
}

namespace RUSS {
    void __cdecl Mox_Update() {}
    void __cdecl Assert_Settings() {}
    void __cdecl Mox_Sync_Update() {}
} // namespace RUSS

namespace COLCALC {
    char* command_summary_msg;
    void __cdecl Compute_Player_Maintenance(s_player*, STRBUILD::s_builder*) {}
} // namespace COLCALC

namespace STRBUILD {
    s_builder Init(char* buffer, uint32_t buffer_size) {
        buffer[0] = '\0';
        return {buffer, buffer, buffer + buffer_size};
    }
} // namespace STRBUILD

namespace SAVEGAME {
    std::string Consume_Loaded_Config_Message() {
        return {};
    }
} // namespace SAVEGAME

namespace TEST_NATIVE {
    int helps = 0; // the game's help messages shown
} // namespace TEST_NATIVE

namespace GENDRAW {
    void __cdecl Message_Box(const char*) {}
    void __cdecl Help(const char*) {
        ++TEST_NATIVE::helps;
    }
    int16_t __cdecl Do_Confirmation_Box(const char*) {
        return 0; // the game menu's questions (leave_game): No
    }
} // namespace GENDRAW

namespace LOADSAVE {
    uint8_t antares_direct_page;
    // Save_Session_Related_Settings: tests/fakes/fake_save_runtime.cpp.
} // namespace LOADSAVE

// FILEDEF::Save_Game: tests/fakes/fake_save_runtime.cpp (it records the slot).

namespace ERIC {
    void __cdecl Fast_Fade_Out() {}
} // namespace ERIC

namespace NETMOX {
    void __cdecl Human_Abdicates(int) {}
} // namespace NETMOX

namespace netcode {
    void __cdecl Net_Uninit() {}
} // namespace netcode

namespace SYS {
    void __cdecl Clear_System_Display_Loaded_Flag() {}
} // namespace SYS

namespace key {
    uint8_t Keyboard_Status() {
        return 0;
    }
    int32_t Read_Key() {
        return 0;
    }
} // namespace key

namespace fields {
    void __cdecl Clear_Mouse_Buffer() {}
} // namespace fields

namespace vfs {
    std::vector<s_package_source> Package_Sources() {
        return {};
    }
} // namespace vfs

namespace antares::host {
    bool Active() {
        return false;
    }
    void On_Publish() {}
    uint64_t Frame_Generation() {
        return 0;
    }
    bool Native_UI() {
        return false;
    }
} // namespace antares::host

namespace fonts {
    void __cdecl Load_Palette(int16_t, int16_t, int16_t) {}
} // namespace fonts

namespace fields {
    void __cdecl Clear_Fields() {}
} // namespace fields

namespace video {
    void __cdecl Set_Page_Off() {}
    void __cdecl Copy_Off_To_Back() {}
    void __cdecl Set_Refresh_Stencil() {}
} // namespace video

namespace TEST_NATIVE {
    int home_star_renames = 0;
} // namespace TEST_NATIVE

namespace namestar {
    int16_t __cdecl Star_Name_Popup_Map_Center_X() {
        return 0;
    }
    int16_t __cdecl Star_Name_Popup_Map_Center_Y() {
        return 0;
    }
    // The first turn's question: counted.
    void __cdecl Change_Home_Star_Name_Popup(int16_t, int16_t, int16_t) {
        ++TEST_NATIVE::home_star_renames;
    }
} // namespace namestar

namespace ERICNET {
    int __cdecl Mox_Play_Sound(int16_t) {
        return 0; // the frontend's button click (play_sound)
    }
} // namespace ERICNET

namespace sound {
    // WP3.9 stage 1: a native battle's beam sounds (native::Play_Game_Sound, Fade_Game_Sound).
    void __cdecl Pitch_Bend_Sound(int32_t, int32_t) {}
    void __cdecl Fadeout_Sound(int16_t, int8_t) {}
    // WP3.13: the title panel's motor stops (commands.cpp, stop_sound).
    void __cdecl Stop_Sound(int32_t) {}
} // namespace sound

namespace FMTPARA {
    // A message's paragraph inserts (native::Message expands "\x18B<n>"): the values a test sets.
    int32_t test_item_values[16] = {};
    uint32_t __cdecl Get_Item_Value(uint16_t index) {
        return index < 16 ? (uint32_t)test_item_values[index] : 0;
    }
} // namespace FMTPARA

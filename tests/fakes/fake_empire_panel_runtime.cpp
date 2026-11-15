#include "pch.h"

// Stand-ins for what src/antares/empire_panel_queries.cpp calls in the game (WP3.7), for
// tests/unit/empire_panel_queries_test.cpp: the spies as BILL keeps them (low 6 bits a count,
// high 2 bits the mission), the officer screen's actions recorded, the questions answered Yes.

namespace TEST_EMPIRE_PANEL {
    int dismissed = 0;
    int assigned = 0;
    int confirmations = 0;
} // namespace TEST_EMPIRE_PANEL

namespace MOX {
    char* treaty_labels[7] = {};
} // namespace MOX

namespace BILL {
    int16_t __cdecl Get_My_Spy_Number(int16_t p) {
        return (int16_t)(MOX::player[MOX::PLAYER_NUM].spies[p] & 0x3F);
    }
    int8_t __cdecl Get_My_Spy_Mission(int16_t p) {
        return (int8_t)((MOX::player[MOX::PLAYER_NUM].spies[p] >> 6) & 3);
    }
    int16_t __cdecl Get_My_Agent_Number() {
        return (int16_t)(MOX::player[MOX::PLAYER_NUM].spies[MOX::PLAYER_NUM] & 0x3F);
    }
    int16_t __cdecl Get_Their_Spy_Number(int16_t, int16_t) {
        return 0;
    }
    void __cdecl Set_My_Spy_Number(int16_t p, uint8_t n) {
        uint8_t& v = (uint8_t&)MOX::player[MOX::PLAYER_NUM].spies[p];
        v = (uint8_t)((v & 0xC0) | (n & 0x3F));
    }
    void __cdecl Set_My_Spy_Mission(int16_t p, int8_t mission) {
        uint8_t& v = (uint8_t&)MOX::player[MOX::PLAYER_NUM].spies[p];
        v = (uint8_t)((v & 0x3F) | ((mission & 3) << 6));
    }
    void __cdecl Set_My_Agent_Number(uint8_t n) {
        MOX::player[MOX::PLAYER_NUM].spies[MOX::PLAYER_NUM] = (int8_t)(n & 0x3F);
    }
} // namespace BILL

namespace GENDRAW {
    int16_t __cdecl Confirmation_Box(const char*) {
        ++TEST_EMPIRE_PANEL::confirmations;
        return 1;
    }
} // namespace GENDRAW

namespace HACCESS {
    bool __cdecl Player_Has_Colony_In_System(int16_t, int16_t system_idx) {
        return system_idx == 0;
    }
} // namespace HACCESS

namespace JIM {
    void __cdecl Decode_Text_Race(char*, int16_t, int16_t) {}
    void __cdecl Decode_Text_Stat(char*, int16_t, int16_t) {}
} // namespace JIM

namespace OFFICER {
    void __cdecl Assign_Captain(int16_t leader_idx, int16_t ship_idx) {
        ++TEST_EMPIRE_PANEL::assigned;
        MOX::ship[ship_idx].officer_index = leader_idx;
    }
    void __cdecl Dismiss_Officer(int16_t leader_idx) {
        ++TEST_EMPIRE_PANEL::dismissed;
        MOX::leaders[leader_idx].status = -1;
    }
    void __cdecl Move_Officer_To_Limbo(int16_t leader_idx) {
        MOX::leaders[leader_idx].status = 2;
        MOX::leaders[leader_idx].location = -1;
    }
    void __cdecl Move_From_Limbo_To_Pool() {
        for (int i = 0; i < MAX_LEADERS; ++i) {
            if (MOX::leaders[i].status == 2) {
                MOX::leaders[i].status = 0;
            }
        }
    }
} // namespace OFFICER

namespace SHIPMOVE {
    bool __cdecl Confirm_Officer_Change(int16_t, int16_t, int8_t) {
        return true;
    }
} // namespace SHIPMOVE

namespace spy {
    void __cdecl Compute_Spy_Bonuses(s_player*, int16_t* spy, int16_t* agent) {
        *spy = 10;
        *agent = 20;
    }
} // namespace spy

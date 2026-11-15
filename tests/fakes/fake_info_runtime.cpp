#include "pch.h"

// Stand-ins for what src/antares/info_queries.cpp calls in the game (WP3.7, the Info panel), for
// tests/unit/info_queries_test.cpp: the history buttons as the game keeps them in history_btns
// (bits 0-3 the graphs, 4-6 the tab), one tech review list, plain trait names.

namespace TEST_INFO {
    s_tech_review_achievement achievements[4] = {
        {TECH_APP_NO_TECH, TECH_REVIEW_GROUP_MISCELLANEOUS},
        {7, 255},
        {8, 255},
        {TECH_APP_NO_TECH, TECH_REVIEW_GROUP_NONE},
    };
} // namespace TEST_INFO

namespace INFO {
    s_tech_review_achievement* review_list[4] = {TEST_INFO::achievements, nullptr, nullptr, nullptr};
    bool history_graph_smoothing = false;

    int32_t __cdecl Smooth_History_Data(int32_t*, int16_t, int16_t) {
        return 0;
    }
    void __cdecl Get_Plyr_Hist_Btns(int16_t* btns) {
        for (int16_t i = 0; i < 4; ++i) {
            btns[i] = MOX::player[MOX::PLAYER_NUM].history_btns & (1 << i);
        }
    }
    void __cdecl Set_Plyr_Hist_Btns(int16_t* btns) {
        for (int16_t i = 0; i < 4; ++i) {
            if (btns[i] != 0) {
                MOX::player[MOX::PLAYER_NUM].history_btns = (int8_t)(MOX::player[MOX::PLAYER_NUM].history_btns | (1 << i));
            } else {
                MOX::player[MOX::PLAYER_NUM].history_btns = (int8_t)(MOX::player[MOX::PLAYER_NUM].history_btns & ~(1 << i));
            }
        }
    }
} // namespace INFO

namespace BILL {
    int16_t __cdecl Get_History_Params(int16_t* length) {
        *length = 3;
        return 0;
    }
    void __cdecl Set_Plyr_Info_Btns(int16_t tab) {
        auto& b = MOX::player[MOX::PLAYER_NUM].history_btns;
        b = (int8_t)(((uint8_t)b & 0x8F) | ((tab & 7) << 4));
    }
} // namespace BILL

namespace raceopt {
    void Format_Numeric_Trait(char* output, size_t size, int16_t, int16_t value) {
        snprintf(output, size, " %+d", value);
    }
} // namespace raceopt

namespace MOX {
    char* help_category_labels[17] = {};
    char* government_labels[GOVERNMENT_COUNT] = {};
    char* special_ability_names[32] = {};
    int16_t bill_savegame[6] = {0, 0, 1, 1, 1, 1};
} // namespace MOX

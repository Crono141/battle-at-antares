#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.7: the Leaders and Races panels' queries and commands (src/antares/empire_panel_queries.cpp).

namespace TEST_EMPIRE_PANEL {
    extern int dismissed;
    extern int assigned;
} // namespace TEST_EMPIRE_PANEL

namespace {
    s_player e_players_[3];
    s_leader_data e_leaders_[MAX_LEADERS];
    s_ship_data e_ships_[MAX_SHIPS];
    s_star_data e_stars_[2];

    bool Has_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(const std::string& json) {
        return antares::native::Run_Command_For_Test(SCREEN_MAIN, json);
    }
} // namespace

DOCTEST_TEST_CASE("the Races panel's spies, ignore and war, the Leaders panel's pool and dismiss (WP3.7)") {
    for (auto& p : e_players_) {
        p = s_player{};
    }
    MOX::player = e_players_;
    MOX::NUM_PLAYERS = 3;
    MOX::PLAYER_NUM = 0;
    MOX::current_screen = SCREEN_MAIN;
    e_players_[0].contact[1] = 1; // met player 1, not player 2
    e_players_[0].spies[0] = 5;   // five at home
    for (auto& s : e_stars_) {
        s = s_star_data{};
    }
    MOX::star = e_stars_;
    MOX::NUM_STARS = 2;
    // Spies: two to player 1 (from home), the mission to sabotage; never more than home has.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"set_spies\",\"player\":1,\"count\":2,\"mission\":1}"), "\"spies\":2,\"agents\":3,\"mission\":1"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"set_spies\",\"player\":1,\"count\":40}"), "\"spies\":5,\"agents\":0"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"set_spies\",\"player\":2,\"count\":1}"), "bad_args")); // not met
    // Ignore toggles; war is an order for the next turn, not twice.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"toggle_ignore\",\"player\":1}"), "\"ignoring\":true"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"toggle_ignore\",\"player\":1}"), "\"ignoring\":false"));
    e_players_[0].delayed_diplomacy_orders[1] = -1;
    // (The shared fake answers the game's question No: nothing is ordered.)
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"declare_war\",\"player\":1}"), "\"declared\":false"));
    DOCTEST_CHECK(e_players_[0].delayed_diplomacy_orders[1] == -1);
    e_players_[0].treaty[1] = 4;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"declare_war\",\"player\":1}"), "refused"));
    // Leaders: a governor at star 0 goes to the pool; a pooled one can be dismissed.
    for (auto& l : e_leaders_) {
        l = s_leader_data{};
        l.status = -1;
        l.player_index = -1;
    }
    e_leaders_[3].player_index = 0;
    e_leaders_[3].type = LEADER_TYPE_COLONY;
    e_leaders_[3].status = 1;
    e_leaders_[3].location = 0;
    MOX::leaders = e_leaders_;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"leader_pool\",\"leader\":3}"), "\"pooled\":true"));
    DOCTEST_CHECK(e_leaders_[3].status == 0);
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"leader_pool\",\"leader\":3}"), "refused")); // no post now
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"leader_dismiss\",\"leader\":3}"), "\"dismissed\":true"));
    DOCTEST_CHECK(TEST_EMPIRE_PANEL::dismissed == 1);
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"leader_pool\",\"leader\":7}"), "bad_args")); // not the player's
}

#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.7: the Info panel's queries and commands (src/antares/info_queries.cpp).

namespace {
    s_player i_players_[3];

    bool Has_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(const std::string& json) {
        return antares::native::Run_Command_For_Test(SCREEN_MAIN, json);
    }
} // namespace

DOCTEST_TEST_CASE("the Info panel: its tab and graph choices, the history, the tech review (WP3.7)") {
    for (auto& p : i_players_) {
        p = s_player{};
    }
    MOX::player = i_players_;
    MOX::NUM_PLAYERS = 3;
    MOX::PLAYER_NUM = 0;
    MOX::current_screen = SCREEN_MAIN;
    i_players_[0].contact[1] = 1; // met player 1; player 2 is dead
    i_players_[2].eliminated = 1;
    i_players_[0].history_btns = 0x4F;
    // The tab goes into bits 4-6, the graphs stay.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"info_set_tab\",\"tab\":2}"), "\"tab\":2"));
    DOCTEST_CHECK(i_players_[0].history_btns == 0x2F);
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"info_set_tab\",\"tab\":7}"), "bad_args"));
    // Graphs: two off; all four off turns them all on, as the screen does.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"info_set_graphs\",\"pop\":0,\"tech\":0}"), "\"graphs\":[0,1,1,0]"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"info_set_graphs\",\"prod\":0,\"fleet\":0}"), "\"graphs\":[1,1,1,1]"));
    DOCTEST_CHECK(i_players_[0].history_btns == 0x2F);
    // History: the player, the met one and the dead one, three points each, summed.
    i_players_[1].population_history[0] = 10;
    i_players_[1].production_history[1] = 30;
    const std::string history = Run_("{\"verb\":\"query\",\"what\":\"history\"}");
    DOCTEST_CHECK(Has_(history, "\"player\":0"));
    DOCTEST_CHECK(Has_(history, "\"player\":1"));
    DOCTEST_CHECK(Has_(history, "\"player\":2"));
    DOCTEST_CHECK(Has_(history, "\"points\":[10,30,0]"));
    DOCTEST_CHECK(Has_(history, "\"top\":50"));
    // Tech review: only researched applications, under their group.
    i_players_[0].tech_applications[8] = TECH_RESEARCH_STATUS_RESEARCHED;
    const std::string review = Run_("{\"verb\":\"query\",\"what\":\"tech_review\"}");
    DOCTEST_CHECK(Has_(review, "\"apps\":[{\"app\":8,"));
    DOCTEST_CHECK(!Has_(review, "\"app\":7"));
}

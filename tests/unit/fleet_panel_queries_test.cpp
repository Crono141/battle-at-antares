#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.5c: the Fleets panel's ship detail and relocation clicks (src/antares/fleet_panel_queries.cpp).

namespace TEST_FLEET_PANEL {
    extern int relocations;
    extern int16_t last_from;
    extern int16_t last_to;
} // namespace TEST_FLEET_PANEL

namespace {
    s_player f_players_[2];
    s_ship_data f_ships_[MAX_SHIPS];
    s_star_data f_stars_[3];

    bool Has_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(const std::string& json) {
        return antares::native::Run_Command_For_Test(SCREEN_MAIN, json);
    }
} // namespace

DOCTEST_TEST_CASE("the Fleets panel's ship detail and relocation (WP3.5c)") {
    for (auto& p : f_players_) {
        p = s_player{};
    }
    MOX::player = f_players_;
    MOX::NUM_PLAYERS = 2;
    MOX::PLAYER_NUM = 0;
    MOX::current_screen = SCREEN_MAIN;
    for (auto& s : f_ships_) {
        s = s_ship_data{};
        s.owner = -1;
        s.officer_index = -1;
    }
    f_ships_[2].owner = 0;
    f_ships_[2].d.ship_type = SHIP_TYPE_COMBAT;
    f_ships_[2].crew_experience = 135;
    snprintf(f_ships_[2].d.name, sizeof(f_ships_[2].d.name), "Scout");
    f_ships_[3].owner = 1;
    MOX::ship = f_ships_;
    MOX::NUM_SHIPS = 4;
    for (auto& s : f_stars_) {
        s = s_star_data{};
    }
    MOX::star = f_stars_;
    MOX::NUM_STARS = 3;
    const std::string detail = Run_("{\"verb\":\"query\",\"what\":\"ship_detail\",\"ship\":2}");
    DOCTEST_CHECK(Has_(detail, "\"name\":\"Scout\""));
    DOCTEST_CHECK(Has_(detail, "\"crew\":\"Regular Crew (135 EP)\""));
    DOCTEST_CHECK(Has_(detail, "\"attack\":40"));
    DOCTEST_CHECK(Has_(detail, "\"defense\":75"));
    DOCTEST_CHECK(Has_(detail, "\"combat\":true"));
    // Another empire's ship: not shown.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"ship_detail\",\"ship\":3}"), "bad_args"));
    // Relocation: from star 0, then to star 2; a reset drops a half-made one.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"relocate_star\",\"star\":0}"), "\"from\":0"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"relocate_star\",\"star\":2}"), "\"set\":true"));
    DOCTEST_CHECK(TEST_FLEET_PANEL::relocations == 1);
    DOCTEST_CHECK(TEST_FLEET_PANEL::last_from == 0);
    DOCTEST_CHECK(TEST_FLEET_PANEL::last_to == 2);
    Run_("{\"verb\":\"relocate_star\",\"star\":1}");
    Run_("{\"verb\":\"relocate_reset\"}");
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"relocate_star\",\"star\":2}"), "\"from\":2"));
    Run_("{\"verb\":\"relocate_reset\"}");
    DOCTEST_CHECK(TEST_FLEET_PANEL::relocations == 1);
}

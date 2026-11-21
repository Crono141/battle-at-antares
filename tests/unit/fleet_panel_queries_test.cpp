#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.5c: the Fleets panel's ship detail and relocation clicks (src/antares/fleet_panel_queries.cpp).

namespace TEST_FLEET_PANEL {
    extern int relocations;
    extern int16_t last_from;
    extern int16_t last_to;
    extern int16_t relocation_from;
    extern int16_t relocation_to;
    extern bool refuse_relocation;
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

// The system pane's relocation (2026-11-20): the targets with their distance, ETA and reasons,
// set through the game's checks, cleared directly.
// The select-combat window's sides (owner, 2026-11-20): ours with damage, theirs by hull, the
// band only with Battle Scanner researched.
DOCTEST_TEST_CASE("combat_fleet lists our ships in orbit with damage, and the others by hull") {
    for (auto& p : f_players_) {
        p = s_player{};
    }
    snprintf(f_players_[1].race_name, sizeof(f_players_[1].race_name), "Silicoid");
    MOX::player = f_players_;
    MOX::NUM_PLAYERS = 2;
    MOX::PLAYER_NUM = 0;
    MOX::current_screen = SCREEN_MAIN;
    for (auto& s : f_ships_) {
        s = s_ship_data{};
        s.owner = -1;
        s.officer_index = -1;
    }
    f_ships_[0].owner = 0; // ours, in orbit at star 1, damaged
    f_ships_[0].location = 1;
    f_ships_[0].d.ship_type = SHIP_TYPE_COMBAT;
    f_ships_[0].d.size = 2;
    f_ships_[0].armor_damage = 40;
    f_ships_[0].structural_damage = 10;
    f_ships_[0].drive_damage_percent = 25;
    snprintf(f_ships_[0].d.name, sizeof(f_ships_[0].d.name), "Widow");
    f_ships_[1].owner = 0; // ours, but on its way: not in the fight
    f_ships_[1].location = 1;
    f_ships_[1].status = 1;
    f_ships_[1].d.ship_type = SHIP_TYPE_COMBAT;
    f_ships_[2].owner = 1; // theirs, in orbit
    f_ships_[2].location = 1;
    f_ships_[2].d.size = 3;
    f_ships_[3].owner = 0; // a colony ship of ours: not a warship
    f_ships_[3].location = 1;
    f_ships_[3].d.ship_type = SHIP_TYPE_COLONY;
    MOX::ship = f_ships_;
    MOX::NUM_SHIPS = 4;
    for (auto& s : f_stars_) {
        s = s_star_data{};
    }
    MOX::star = f_stars_;
    MOX::NUM_STARS = 3;
    const std::string fleet = Run_("{\"verb\":\"query\",\"what\":\"combat_fleet\",\"star\":1}");
    DOCTEST_CHECK(Has_(fleet, "\"scanner\":false"));
    DOCTEST_CHECK(Has_(fleet, "\"ship\":0,\"name\":\"Widow\""));
    DOCTEST_CHECK(Has_(fleet, "\"armor\":60,\"armor_max\":100,\"structure\":50,\"structure_max\":60"));
    DOCTEST_CHECK(Has_(fleet, "Drive 25% damaged"));
    DOCTEST_CHECK_FALSE(Has_(fleet, "\"ship\":1,")); // on its way
    DOCTEST_CHECK_FALSE(Has_(fleet, "\"ship\":3,")); // not a warship
    DOCTEST_CHECK(Has_(fleet, "\"player\":1,\"name\":\"Silicoid\""));
    DOCTEST_CHECK(Has_(fleet, "\"ships\":1"));
    DOCTEST_CHECK_FALSE(Has_(fleet, "\"strength\""));
    // Only the sides in the fight: another side not asked for is left out.
    DOCTEST_CHECK_FALSE(Has_(Run_("{\"verb\":\"query\",\"what\":\"combat_fleet\",\"star\":1,\"sides\":[5]}"), "Silicoid"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"combat_fleet\",\"star\":1,\"sides\":[1]}"), "Silicoid"));
    f_players_[0].tech_applications[TECH_APP_BATTLE_SCANNER] = TECH_RESEARCH_STATUS_RESEARCHED;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"combat_fleet\",\"star\":1}"), "\"strength\":\"superior\"")); // 30 vs 10
}

DOCTEST_TEST_CASE("relocation from the system pane: targets, set and clear") {
    for (auto& p : f_players_) {
        p = s_player{};
    }
    f_players_[0].ship_speed = 2;
    MOX::player = f_players_;
    MOX::NUM_PLAYERS = 2;
    MOX::PLAYER_NUM = 0;
    MOX::current_screen = SCREEN_MAIN;
    for (auto& s : f_stars_) {
        s = s_star_data{};
    }
    f_stars_[1].spectral_class = STAR_CLASS_BLACK_HOLE;
    MOX::star = f_stars_;
    MOX::NUM_STARS = 3;
    // Star 2 is two parsecs from star 0 in the fake: one turn at speed 2. Unvisited stars are
    // refused with a reason, the black hole too.
    const std::string targets = Run_("{\"verb\":\"query\",\"what\":\"relocation_targets\",\"from\":0}");
    DOCTEST_CHECK(Has_(targets, "\"speed\":2"));
    DOCTEST_CHECK(Has_(targets, "\"reason\":\"A black hole\""));
    DOCTEST_CHECK(Has_(targets, "\"star\":2,\"allowed\":false,\"reason\":\"Not explored\""));
    DOCTEST_CHECK(Has_(targets, "\"parsecs\":2,\"eta\":1"));
    // Set: the game's checks pass, the relocation is made; the same star twice is refused.
    TEST_FLEET_PANEL::refuse_relocation = false;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"relocation_set\",\"from\":0,\"to\":2}"), "\"set\":true"));
    DOCTEST_CHECK(TEST_FLEET_PANEL::relocation_from == 0);
    DOCTEST_CHECK(TEST_FLEET_PANEL::relocation_to == 2);
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"relocation_set\",\"from\":0,\"to\":0}"), "bad_args"));
    // The game refuses (its own message): nothing changes.
    TEST_FLEET_PANEL::refuse_relocation = true;
    TEST_FLEET_PANEL::relocation_to = 5;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"relocation_set\",\"from\":0,\"to\":2}"), "\"set\":false"));
    DOCTEST_CHECK(TEST_FLEET_PANEL::relocation_to == 5);
    TEST_FLEET_PANEL::refuse_relocation = false;
    // Clear.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"relocation_clear\",\"from\":0}"), "\"cleared\":true"));
    DOCTEST_CHECK(TEST_FLEET_PANEL::relocation_to == -1);
}

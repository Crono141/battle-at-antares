#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.4: the colony pane's query and commands (src/antares/colony_queries.cpp).

namespace TEST_COLONY {
    extern int colony_calculations;
    extern int buys;
    extern bool refuse_job;
} // namespace TEST_COLONY

namespace {
    s_player c_players_[2];
    s_star_data c_stars_[1];
    s_planet_data c_planets_[2];
    s_colony c_colonies_[2];
    char c_building_names_[49][16];

    bool Has_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(const std::string& json) {
        return antares::native::Run_Command_For_Test(SCREEN_MAIN, json);
    }

    uint32_t Pop_(int race, int job) {
        return (uint32_t)race | ((uint32_t)job << 7) | 0x200u; // assigned
    }

    // Colony 0: the player's, three farmers, a worker and an android scientist; colony 1 another
    // player's.
    void Set_Up_() {
        for (auto& p : c_players_) {
            p = s_player{};
        }
        c_players_[0].race = 9;
        c_players_[0].color = 2;
        snprintf(c_players_[0].race_name, sizeof(c_players_[0].race_name), "Psilon");
        MOX::player = c_players_;
        MOX::NUM_PLAYERS = 2;
        MOX::PLAYER_NUM = 0;
        MOX::current_screen = SCREEN_MAIN;
        c_stars_[0] = s_star_data{};
        MOX::star = c_stars_;
        MOX::NUM_STARS = 1;
        for (auto& p : c_planets_) {
            p = s_planet_data{};
            p.star_index = 0;
        }
        MOX::planet = c_planets_;
        MOX::planet_count = 2;
        for (auto& c : c_colonies_) {
            c = s_colony{};
        }
        s_colony& col = c_colonies_[0];
        col.owner = 0;
        col.planet = 0;
        col.n_pops = 5;
        col.pop[0] = Pop_(0, 0);
        col.pop[1] = Pop_(0, 0);
        col.pop[2] = Pop_(0, 0);
        col.pop[3] = Pop_(0, 1);
        col.pop[4] = Pop_(8, 2); // android
        col.max_population = 12;
        col.max_farms = 255;
        col.morale = 6;
        col.production[ECON_FOOD] = 23;
        col.production[ECON_INDUSTRY] = 15;
        col.maintenance[ECON_INDUSTRY] = 3;
        col.pollution = 2;
        col.producing[0] = BUILDING_STAR_BASE;
        for (int i = 1; i < 7; ++i) {
            col.producing[i] = COLONY_PRODUCTION_NONE;
        }
        col.production_spent = 120;
        col.buildings[BUILDING_ARTEMIS_SYSTEM_NET] = 1;
        col.buildings[BUILDING_MARINE_BARRACKS] = 1;
        col.military[0] = 13;
        col.military[1] = 6;
        c_colonies_[1].owner = 1;
        MOX::colony = c_colonies_;
        MOX::NUM_COLONIES = 2;
        for (int i = 0; i < 49; ++i) {
            snprintf(c_building_names_[i], sizeof(c_building_names_[i]), "B%d", i);
            TECHDATA::buildings[i].name = c_building_names_[i];
            TECHDATA::buildings[i].group = 0;
        }
        TECHDATA::buildings[BUILDING_STAR_BASE].group = 7;
        TECHDATA::buildings[BUILDING_ARTEMIS_SYSTEM_NET].group = 7;
        TECHDATA::buildings[BUILDING_DIMENSIONAL_PORTAL].group = 7;
        TEST_COLONY::colony_calculations = 0;
        TEST_COLONY::buys = 0;
        TEST_COLONY::refuse_job = false;
    }
} // namespace

DOCTEST_TEST_CASE("the colony query reads the colony screen's values (WP3.4)") {
    Set_Up_();
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"colony\",\"colony\":1}"), "bad_args")); // not the player's
    const std::string c = Run_("{\"verb\":\"query\",\"what\":\"colony\",\"colony\":0}");
    DOCTEST_CHECK(Has_(c, "\"race\":\"Psilon\""));
    DOCTEST_CHECK(Has_(c, "\"population\":5"));
    // Food 23: two tens (bags) and three units; industry 15 - 3 upkeep: 12, then pollution.
    DOCTEST_CHECK(Has_(c, "\"food\":{\"produced\":23,\"upkeep\":0,\"imports\":0,\"icons\":[{\"kind\":\"net\",\"sprite\":4,\"count\":2},{\"kind\":\"net\",\"sprite\":0,\"count\":3},{\"kind\":\"gap\"}]}"));
    DOCTEST_CHECK(Has_(c, "\"icons\":[{\"kind\":\"net\",\"sprite\":5,\"count\":1},{\"kind\":\"net\",\"sprite\":1,\"count\":2},{\"kind\":\"pollution\",\"sprite\":9,\"count\":2},{\"kind\":\"gap\"},{\"kind\":\"gap\"}]"));
    // Jobs: three farmers, one worker (race 9 * 13 + job * 2 + 1), the android scientist.
    DOCTEST_CHECK(Has_(c, "{\"job\":0,\"colonists\":[{\"pop\":0,\"sprite\":118,\"state\":\"citizen\"},{\"pop\":1,"));
    DOCTEST_CHECK(Has_(c, "{\"job\":1,\"colonists\":[{\"pop\":3,\"sprite\":120,\"state\":\"citizen\"}]}"));
    DOCTEST_CHECK(Has_(c, "{\"job\":2,\"colonists\":[{\"pop\":4,\"sprite\":169,\"state\":\"android\"}]}"));
    // The build: the star base, with its price; its picture is the satellite's (COLONY.LBX 9 + 2).
    DOCTEST_CHECK(Has_(c, "\"build\":{\"id\":40,\"name\":\"B40\",\"auto\":false,\"picture\":{\"lbx\":\"COLONY.LBX\",\"entry\":11},\"cost\":500,\"spent\":120,\"bought\":0,\"turns\":4,\"buy_cost\":1200"));
    // Defences: the star base not built, Artemis built, no portal.
    DOCTEST_CHECK(Has_(c, "{\"building\":40,\"name\":\"B40\",\"built\":false,\"sprite\":11}"));
    DOCTEST_CHECK(Has_(c, "{\"building\":3,\"name\":\"B3\",\"built\":true,\"sprite\":16}"));
    // Garrison: race 9's marines (9 * 13 + 7) and tanks (+ 9).
    DOCTEST_CHECK(Has_(c, "\"garrison\":{\"marines\":13,\"marine_sprite\":124,\"armor\":6,\"armor_sprite\":126}"));
    // Buildings: the barracks, not the satellites.
    DOCTEST_CHECK(Has_(c, "\"buildings\":[{\"building\":22,\"name\":\"B22\"}]"));
}

DOCTEST_TEST_CASE("move_colonist and buy are the colony screen's actions (WP3.4)") {
    Set_Up_();
    // A farmer to research.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"move_colonist\",\"colony\":0,\"pop\":0,\"job\":2}"), "\"moved\":true"));
    DOCTEST_CHECK(((c_colonies_[0].pop[0] >> 7) & 3) == 2);
    DOCTEST_CHECK((c_colonies_[0].pop[0] & 0x200) != 0);
    DOCTEST_CHECK(TEST_COLONY::colony_calculations == 1);
    // A refusal (the game's help message) puts it back where it was, still assigned.
    TEST_COLONY::refuse_job = true;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"move_colonist\",\"colony\":0,\"pop\":1,\"job\":1}"), "\"moved\":false"));
    DOCTEST_CHECK(((c_colonies_[0].pop[1] >> 7) & 3) == 0);
    DOCTEST_CHECK((c_colonies_[0].pop[1] & 0x200) != 0);
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"move_colonist\",\"colony\":1,\"pop\":0,\"job\":1}"), "bad_args"));
    // BUY, then BUY again un-buys.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"buy\",\"colony\":0}"), "\"bought\":true"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"buy\",\"colony\":0}"), "\"bought\":false"));
    DOCTEST_CHECK(TEST_COLONY::buys == 2);
}

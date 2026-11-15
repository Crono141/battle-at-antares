#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.5b: the Planets panel's query and commands (src/antares/planet_queries.cpp).

namespace TEST_PLANETS {
    extern bool free_colony;
    extern bool flux;
    extern int sends;
    extern int recalls;
    extern int warnings;
    extern int8_t en_route_colony[8];
} // namespace TEST_PLANETS

namespace {
    s_player p_players_[2];
    s_star_data p_stars_[1];
    s_planet_data p_planets_[4];
    s_colony p_colonies_[2];

    bool Has_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(const std::string& json) {
        return antares::native::Run_Command_For_Test(SCREEN_MAIN, json);
    }

    // Planet 0 the player's colony, 1 free and heavy-G, 2 another empire's colony, 3 free and
    // hostile (no food).
    void Set_Up_() {
        for (auto& p : p_players_) {
            p = s_player{};
        }
        MOX::player = p_players_;
        MOX::NUM_PLAYERS = 2;
        MOX::PLAYER_NUM = 0;
        MOX::current_screen = SCREEN_MAIN;
        p_stars_[0] = s_star_data{};
        MOX::star = p_stars_;
        MOX::NUM_STARS = 1;
        for (auto& p : p_planets_) {
            p = s_planet_data{};
            p.planet_type = PLANET_TYPE_PLANET;
            p.colony_index = -1;
            p.food_per_farmer = 4;
            p.gravity_class = PLANET_GRAVITY_NORMAL_G;
            p.mineral_class = MINERAL_RESOURCE_ABUNDANT;
        }
        p_planets_[0].colony_index = 0;
        p_planets_[1].gravity_class = PLANET_GRAVITY_HEAVY_G;
        p_planets_[2].colony_index = 1;
        p_planets_[3].food_per_farmer = 0;
        MOX::planet = p_planets_;
        MOX::planet_count = 4;
        p_colonies_[0] = s_colony{};
        p_colonies_[0].owner = 0;
        p_colonies_[1] = s_colony{};
        p_colonies_[1].owner = 1;
        MOX::colony = p_colonies_;
        MOX::NUM_COLONIES = 2;
        TEST_PLANETS::free_colony = true;
        TEST_PLANETS::flux = false;
        TEST_PLANETS::sends = 0;
        TEST_PLANETS::recalls = 0;
        TEST_PLANETS::warnings = 0;
        for (auto& e : TEST_PLANETS::en_route_colony) {
            e = -1;
        }
    }
} // namespace

DOCTEST_TEST_CASE("the Planets panel's list, sends and recalls (WP3.5b)") {
    Set_Up_();
    const std::string list = Run_("{\"verb\":\"query\",\"what\":\"planets\"}");
    DOCTEST_CHECK(Has_(list, "\"ok\":true"));
    DOCTEST_CHECK(!Has_(list, "\"planet\":0,")); // the player's own colony
    DOCTEST_CHECK(Has_(list, "\"planet\":1,"));
    DOCTEST_CHECK(Has_(list, "\"planet\":2,"));
    DOCTEST_CHECK(Has_(list, "\"planet\":3,"));
    DOCTEST_CHECK(Has_(list, "\"colony_ships\":true"));
    DOCTEST_CHECK(Has_(list, "\"best_colony_eta\":3")); // 1003: a moving ship's bias dropped
    DOCTEST_CHECK(Has_(list, "\"adverse_gravity\":true"));
    DOCTEST_CHECK(Has_(list, "\"enemy\":true"));
    DOCTEST_CHECK(Has_(list, "\"hostile\":true"));
    // Send a colony ship to planet 1, then it shows on its way; recall it.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"planets_send\",\"planet\":1,\"kind\":\"colony\"}"), "\"sent\":true"));
    DOCTEST_CHECK(TEST_PLANETS::sends == 1);
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"planets\"}"), "\"colony_eta\":3"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"planets_recall\",\"planet\":1,\"kind\":\"colony\"}"), "\"recalled\":true"));
    DOCTEST_CHECK(TEST_PLANETS::recalls == 1);
    // Nothing on its way: no recall; no free ship: no send; the flux: the screen's warning.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"planets_recall\",\"planet\":1,\"kind\":\"colony\"}"), "refused"));
    TEST_PLANETS::free_colony = false;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"planets_send\",\"planet\":1,\"kind\":\"colony\"}"), "refused"));
    TEST_PLANETS::flux = true;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"planets_send\",\"planet\":3,\"kind\":\"colony\"}"), "\"sent\":false"));
    DOCTEST_CHECK(TEST_PLANETS::warnings == 1);
    DOCTEST_CHECK(TEST_PLANETS::sends == 1);
    // Not a planet the screen lists: the player's own colony.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"planets_send\",\"planet\":0,\"kind\":\"colony\"}"), "bad_args"));
}

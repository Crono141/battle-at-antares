#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <set>
#include <string>
#include <vector>

// WP3.3a: the native galaxy's queries and commands (src/antares/galaxy_queries.cpp) against the
// stand-ins in tests/fakes/fake_galaxy_runtime.cpp. What the rules functions themselves do is
// checked by the rules replay (select_star scenario); these cover the arguments, the errors and
// the decisions the commands make around those calls.

namespace TEST_GALAXY {
    extern std::set<int16_t> visited;
    extern std::set<int16_t> viewable;
    extern int16_t last_colony;
    extern int last_colony_calls;
    extern std::vector<int16_t> colony_screens;
    extern std::vector<int16_t> toggles;
    extern int fleet_deselects;
    extern int box_to_top;
    extern int box_vanished;
    extern int16_t cycle_to;
} // namespace TEST_GALAXY

namespace {
    s_player g_players_[2];
    s_star_data g_stars_[3];
    s_planet_data g_planets_[3];
    s_colony g_colonies_[2];
    s_nebula g_nebulae_[1];
    char g_text_[] = "text";

    bool Contains_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(int16_t screen, const std::string& json) {
        return antares::native::Run_Command_For_Test(screen, json);
    }

    // Star 0: the player's (visited, viewable), with planet 0 (the player's colony 0) and planet 1
    // (a gas giant). Star 1: unvisited, planet 2 the other player's outpost colony 1. Star 2: a
    // black hole.
    void Set_Up_Galaxy_() {
        for (auto& p : g_players_) {
            p = s_player{};
        }
        g_players_[1].color = 3;
        MOX::player = g_players_;
        MOX::NUM_PLAYERS = 2;
        MOX::PLAYER_NUM = 0;
        MOX::current_screen = SCREEN_MAIN;
        for (int i = 0; i < 3; ++i) {
            g_stars_[i] = s_star_data{};
            snprintf(g_stars_[i].name, sizeof(g_stars_[i].name), "Star%d", i);
            g_stars_[i].x = (int16_t)(100 * (i + 1));
            g_stars_[i].y = 50;
            g_stars_[i].owner = -1;
            g_stars_[i].wormhole_star_id = -1;
            for (auto& slot : g_stars_[i].planet_index) {
                slot = -1;
            }
            for (auto& officer : g_stars_[i].officer_index) {
                officer = -1;
            }
            g_planets_[i] = s_planet_data{};
            g_planets_[i].colony_index = -1;
            g_planets_[i].planet_type = PLANET_TYPE_PLANET;
            g_planets_[i].max_population = 12;
        }
        g_stars_[0].owner = 0;
        g_stars_[0].spectral_class = 2;
        g_stars_[0].size = 1;
        g_stars_[0].planet_index[0] = 0;
        g_stars_[0].planet_index[3] = 1;
        g_stars_[1].planet_index[2] = 2;
        g_stars_[2].spectral_class = STAR_CLASS_BLACK_HOLE;
        g_planets_[0].star_index = 0;
        g_planets_[0].colony_index = 0;
        g_planets_[1].star_index = 0;
        g_planets_[1].orbit = 3;
        g_planets_[1].planet_type = PLANET_TYPE_GAS_GIANT;
        g_planets_[2].star_index = 1;
        g_planets_[2].colony_index = 1;
        g_colonies_[0] = s_colony{};
        g_colonies_[0].owner = 0;
        g_colonies_[0].planet = 0;
        g_colonies_[0].n_pops = 5;
        g_colonies_[1] = s_colony{};
        g_colonies_[1].owner = 1;
        g_colonies_[1].planet = 2;
        g_colonies_[1].outpost_flag = 1;
        MOX::star = g_stars_;
        MOX::NUM_STARS = 3;
        MOX::planet = g_planets_;
        MOX::planet_count = 3;
        MOX::colony = g_colonies_;
        MOX::NUM_COLONIES = 2;
        g_nebulae_[0] = s_nebula{40, 60, 2};
        MOX::nebula = g_nebulae_;
        MOX::NUM_NEBULAS = 1;
        MOX::ship_icon_count = 0;
        MOX::settings.auto_select_colony = 0;
        MOX::moveable_box[0].id = -1;
        MOX::system_star_id_for_auto_select = -1;
        for (auto* table : {MOX::planet_size_string, MOX::planet_climate_string}) {
            for (int i = 0; i < PLANET_SIZE_COUNT; ++i) {
                table[i] = g_text_;
            }
        }
        for (int i = 0; i < PLANET_CLIMATE_COUNT; ++i) {
            MOX::planet_climate_string[i] = g_text_;
        }
        for (int i = 0; i < MINERAL_RESOURCE_COUNT; ++i) {
            MOX::mineral_class_string[i] = g_text_;
        }
        for (int i = 0; i < PLANET_GRAVITY_COUNT; ++i) {
            MOX::planet_gravity_string[i] = g_text_;
        }
        TEST_GALAXY::visited = {0};
        TEST_GALAXY::viewable = {0};
        TEST_GALAXY::last_colony = -1;
        TEST_GALAXY::last_colony_calls = 0;
        TEST_GALAXY::colony_screens.clear();
        TEST_GALAXY::toggles.clear();
        TEST_GALAXY::fleet_deselects = 0;
        TEST_GALAXY::box_to_top = 0;
        TEST_GALAXY::box_vanished = 0;
        TEST_GALAXY::cycle_to = -1;
    }

    void Tear_Down_Galaxy_() {
        MOX::player = nullptr;
        MOX::star = nullptr;
        MOX::NUM_STARS = 0;
        MOX::planet = nullptr;
        MOX::planet_count = 0;
        MOX::colony = nullptr;
        MOX::NUM_COLONIES = 0;
        MOX::NUM_NEBULAS = 0;
    }
} // namespace

DOCTEST_TEST_CASE("the galaxy query labels stars as the legacy map does") {
    Set_Up_Galaxy_();
    const std::string galaxy = Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"galaxy\"}");
    DOCTEST_CHECK(Contains_(galaxy, "\"ok\":true"));
    // The player's visited star is named; an unvisited star with no known colony and a black hole
    // are not.
    DOCTEST_CHECK(Contains_(galaxy, "\"label\":\"Star0\""));
    DOCTEST_CHECK_FALSE(Contains_(galaxy, "\"label\":\"Star1\""));
    DOCTEST_CHECK_FALSE(Contains_(galaxy, "\"label\":\"Star2\""));
    // Pictures: class 2, size 1 is BUFFER0.LBX 148 + 2 * 6 + 1; a black hole has no size offset.
    DOCTEST_CHECK(Contains_(galaxy, "\"sprite\":161"));
    DOCTEST_CHECK(Contains_(galaxy, "\"sprite\":184"));
    // The nebula's picture is STARBG.LBX 6 + type * 4, placed by its top-left.
    DOCTEST_CHECK(Contains_(galaxy, "{\"type\":2,\"x\":40,\"y\":60,\"sprite\":14}"));
    Tear_Down_Galaxy_();
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"galaxy\"}"), "\"error\":\"no_game\""));
}

DOCTEST_TEST_CASE("a star click opens the system panel and keeps the saved box record") {
    Set_Up_Galaxy_();
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"select_star\"}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"select_star\",\"star\":3}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_(SCREEN_FLEET, "{\"verb\":\"select_star\",\"star\":0}"), "\"error\":\"wrong_screen\""));

    // Auto-select colony off: the system, as Do_System_Popup records it (box 0 on the star, placed
    // and on top, fleet ships deselected), and the F5/F6 start when the system is viewable.
    const std::string system = Run_(SCREEN_MAIN, "{\"verb\":\"select_star\",\"star\":0}");
    DOCTEST_CHECK(Contains_(system, "\"opened\":\"system\""));
    DOCTEST_CHECK(TEST_GALAXY::last_colony_calls == 0);
    DOCTEST_CHECK(MOX::moveable_box[0].id == 0);
    DOCTEST_CHECK(MOX::moveable_box[0].x == 100);
    DOCTEST_CHECK(TEST_GALAXY::box_to_top == 1);
    DOCTEST_CHECK(TEST_GALAXY::fleet_deselects == 1);
    DOCTEST_CHECK(MOX::system_star_id_for_auto_select == 0);
    // An unviewable system leaves the F5/F6 start where it was.
    Run_(SCREEN_MAIN, "{\"verb\":\"select_star\",\"star\":1}");
    DOCTEST_CHECK(MOX::system_star_id_for_auto_select == 0);
    // Closing the panel closes the record.
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"close_system\"}"), "\"ok\":true"));
    DOCTEST_CHECK(TEST_GALAXY::box_vanished == 1);

    // Auto-select colony on: the rules pick the colony (and may record the choice), its screen
    // opens; black holes never ask.
    MOX::settings.auto_select_colony = 1;
    TEST_GALAXY::last_colony = 0;
    const std::string colony = Run_(SCREEN_MAIN, "{\"verb\":\"select_star\",\"star\":0}");
    DOCTEST_CHECK(Contains_(colony, "\"opened\":\"colony\""));
    DOCTEST_CHECK(TEST_GALAXY::colony_screens == std::vector<int16_t>{0});
    DOCTEST_CHECK(MOX::current_screen == SCREEN_COLONY);
    MOX::current_screen = SCREEN_MAIN;
    TEST_GALAXY::last_colony_calls = 0;
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"select_star\",\"star\":2}"), "\"opened\":\"system\""));
    DOCTEST_CHECK(TEST_GALAXY::last_colony_calls == 0);
    Tear_Down_Galaxy_();
}

DOCTEST_TEST_CASE("open_colony opens only the player's colonies") {
    Set_Up_Galaxy_();
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"open_colony\",\"planet\":2}"), "\"error\":\"refused\""));
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"open_colony\",\"planet\":1}"), "\"error\":\"refused\""));
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"open_colony\",\"planet\":7}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(TEST_GALAXY::colony_screens.empty());
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"open_colony\",\"planet\":0}"), "\"opened\":\"colony\""));
    DOCTEST_CHECK(MOX::current_screen == SCREEN_COLONY);
    // One of the player's outposts: its message instead (a request), and the galaxy stays.
    g_colonies_[0].outpost_flag = 1;
    MOX::current_screen = SCREEN_MAIN;
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"open_colony\",\"planet\":0}"), "\"opened\":\"none\""));
    DOCTEST_CHECK(MOX::current_screen == SCREEN_MAIN);
    Tear_Down_Galaxy_();
}

DOCTEST_TEST_CASE("F5 and F6 cycle from the last system shown") {
    Set_Up_Galaxy_();
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"cycle_colony_star\",\"dir\":2}"), "\"error\":\"bad_args\""));
    TEST_GALAXY::cycle_to = -1;
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"cycle_colony_star\",\"dir\":0}"), "\"opened\":\"none\""));
    TEST_GALAXY::cycle_to = 0;
    const std::string result = Run_(SCREEN_MAIN, "{\"verb\":\"cycle_colony_star\",\"dir\":1}");
    DOCTEST_CHECK(Contains_(result, "\"opened\":\"system\""));
    DOCTEST_CHECK(Contains_(result, "\"star\":0"));
    DOCTEST_CHECK(MOX::moveable_box[0].id == 0);
    Tear_Down_Galaxy_();
}

DOCTEST_TEST_CASE("toggle_option takes the Alt+F1..F8 options") {
    Set_Up_Galaxy_();
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"toggle_option\",\"option\":3}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"toggle_option\",\"option\":8}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(TEST_GALAXY::toggles.empty());
    const std::string result = Run_(SCREEN_MAIN, "{\"verb\":\"toggle_option\",\"option\":6}");
    DOCTEST_CHECK(Contains_(result, "\"on\":true"));
    DOCTEST_CHECK(TEST_GALAXY::toggles == std::vector<int16_t>{6});
    Tear_Down_Galaxy_();
}

DOCTEST_TEST_CASE("the system query shows what the box shows") {
    Set_Up_Galaxy_();
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"system\"}"), "\"error\":\"bad_args\""));
    // Unvisited: hidden, with the star's description and no planets.
    const std::string hidden = Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"system\",\"star\":1}");
    DOCTEST_CHECK(Contains_(hidden, "\"view\":\"hidden\""));
    DOCTEST_CHECK(Contains_(hidden, "\"description\":"));
    DOCTEST_CHECK_FALSE(Contains_(hidden, "\"planets\""));
    // Viewable: its planets with the box's text, pictures and places.
    const std::string full = Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"system\",\"star\":0}");
    DOCTEST_CHECK(Contains_(full, "\"view\":\"full\""));
    DOCTEST_CHECK(Contains_(full, "\"type\":\"gas_giant\""));
    DOCTEST_CHECK(Contains_(full, "\"mine\":true"));
    DOCTEST_CHECK(Contains_(full, "\"population\":5"));
    DOCTEST_CHECK(Contains_(full, "\"ring_sprite\":80")); // 0x4D + slot 3
    DOCTEST_CHECK(Contains_(full, "\"lines\":[{\"text\":"));
    // Colony information: not for gas giants.
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"potential_colony\",\"planet\":1}"), "\"error\":\"refused\""));
    const std::string potential = Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"potential_colony\",\"planet\":0}");
    DOCTEST_CHECK(Contains_(potential, "\"max_population\":12"));
    DOCTEST_CHECK(Contains_(potential, "\"industry_per_worker\":3"));
    DOCTEST_CHECK(Contains_(potential, "\"planet\":0"));
    Tear_Down_Galaxy_();
}

DOCTEST_TEST_CASE("the system query v3: governor, planet words, morale and government (WP3.3f)") {
    Set_Up_Galaxy_();
    std::vector<s_leader_data> leaders(MAX_LEADERS);
    snprintf(leaders[4].name, sizeof(leaders[4].name), "Androgena");
    snprintf(leaders[4].title, sizeof(leaders[4].title), "Governor");
    leaders[4].pict_num = 7;
    s_leader_data* saved_leaders = MOX::leaders;
    MOX::leaders = leaders.data();
    // No governor: none in the answer.
    DOCTEST_CHECK_FALSE(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"system\",\"star\":0}"), "\"governor\""));
    g_stars_[0].officer_index[0] = 4;
    g_players_[0].traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_DEMOCRACY;
    g_colonies_[0].morale = 6;
    const std::string full = Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"system\",\"star\":0}");
    DOCTEST_CHECK(Contains_(full, "\"governor\":{\"leader\":4,\"name\":\"Androgena\",\"title\":\"Governor\""));
    DOCTEST_CHECK(Contains_(full, "\"portrait\":217")); // 0xD2 + 7
    DOCTEST_CHECK(Contains_(full, "\"size_name\":"));
    DOCTEST_CHECK(Contains_(full, ("\"government_sprite\":" + std::to_string(0x13 + GOVERNMENT_DEMOCRACY)).c_str()));
    DOCTEST_CHECK(Contains_(full, "\"morale_icons\":3"));
    DOCTEST_CHECK(Contains_(full, "\"morale_sprite\":16"));
    // From Unification up the masks don't show.
    g_players_[0].traits[TRAIT_CURRENT_GOVERNMENT] = GOVERNMENT_UNIFICATION;
    DOCTEST_CHECK(Contains_(Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"system\",\"star\":0}"), "\"morale_icons\":0"));
    MOX::leaders = saved_leaders;
    Tear_Down_Galaxy_();
}

DOCTEST_TEST_CASE("the designs query lists the player's design slots (WP3.3f)") {
    Set_Up_Galaxy_();
    static char hull_name[] = "Cruiser";
    TECHDATA::hull_data[SHIP_SIZE_LARGE].name = hull_name;
    g_players_[0].color = 2;
    for (auto& design : g_players_[0].ship_designs) {
        design = s_ship_design{};
    }
    snprintf(g_players_[0].ship_designs[2].name, sizeof(g_players_[0].ship_designs[2].name), "Corvette VII");
    g_players_[0].ship_designs[2].size = SHIP_SIZE_LARGE;
    g_players_[0].ship_designs[2].picture_num = 5;
    g_players_[0].ship_designs[2].cost = 529;
    const std::string designs = Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"designs\"}");
    DOCTEST_CHECK(Contains_(designs, "\"designs\":[{\"slot\":2,\"name\":\"Corvette VII\",\"hull\":2,\"hull_name\":\"Cruiser\",\"picture\":105,\"cost\":529}]"));
    Tear_Down_Galaxy_();
}

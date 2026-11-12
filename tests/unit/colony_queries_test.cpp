#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>
#include <vector>

// WP3.4: the colony pane's query and commands (src/antares/colony_queries.cpp).

namespace TEST_COLONY {
    extern int colony_calculations;
    extern int buys;
    extern bool refuse_job;
    extern int refuse_after;
    extern int sells;
    extern bool answer_sell;
    extern std::vector<std::string> build_calls;
} // namespace TEST_COLONY

namespace TEST_NATIVE {
    extern uint32_t random_seed;
    extern std::vector<uint32_t> seeds_set;
} // namespace TEST_NATIVE

namespace {
    s_player c_players_[2];
    s_star_data c_stars_[1];
    s_planet_data c_planets_[2];
    s_colony c_colonies_[2];
    char c_building_names_[49][16];
    s_ship_data c_ships_[MAX_SHIPS];

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
        for (auto& s : c_ships_) {
            s = s_ship_data{};
            s.owner = -1;
        }
        MOX::ship = c_ships_;
        TEST_COLONY::colony_calculations = 0;
        TEST_COLONY::buys = 0;
        TEST_COLONY::refuse_job = false;
        TEST_COLONY::refuse_after = -1;
        TEST_COLONY::sells = 0;
        TEST_COLONY::answer_sell = true;
        TECHDATA::buildings[BUILDING_MARINE_BARRACKS].cost = 300;
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
    // Each with what a click picks up there (COLMOVE::Get_Cluster): it and the identical ones after it.
    DOCTEST_CHECK(Has_(c, "{\"job\":0,\"colonists\":[{\"pop\":0,\"sprite\":118,\"state\":\"citizen\",\"cluster\":[0,1,2]},{\"pop\":1,"));
    DOCTEST_CHECK(Has_(c, "{\"pop\":2,\"sprite\":118,\"state\":\"citizen\",\"cluster\":[2]}"));
    DOCTEST_CHECK(Has_(c, "{\"job\":1,\"colonists\":[{\"pop\":3,\"sprite\":120,\"state\":\"citizen\",\"cluster\":[3]}]}"));
    DOCTEST_CHECK(Has_(c, "{\"job\":2,\"colonists\":[{\"pop\":4,\"sprite\":169,\"state\":\"android\",\"cluster\":[4]}]}"));
    // The build: the star base, with its price; its picture is the satellite's (COLONY.LBX 9 + 2).
    DOCTEST_CHECK(Has_(c, "\"build\":{\"id\":40,\"name\":\"B40\",\"auto\":false,\"picture\":{\"lbx\":\"COLONY.LBX\",\"entry\":11},\"cost\":500,\"spent\":120,\"bought\":0,\"turns\":4,\"buy_cost\":1200"));
    // Defences: the star base not built, Artemis built, no portal.
    DOCTEST_CHECK(Has_(c, "{\"building\":40,\"name\":\"B40\",\"built\":false,\"sprite\":11}"));
    DOCTEST_CHECK(Has_(c, "{\"building\":3,\"name\":\"B3\",\"built\":true,\"sprite\":16}"));
    // Garrison: race 9's marines (9 * 13 + 7) and tanks (+ 9).
    DOCTEST_CHECK(Has_(c, "\"garrison\":{\"marines\":13,\"marine_sprite\":124,\"armor\":6,\"armor_sprite\":126}"));
    // Buildings: the barracks, not the satellites.
    DOCTEST_CHECK(Has_(c, "\"buildings\":[{\"building\":22,\"name\":\"B22\",\"sellable\":true,\"refund\":150,"));
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

DOCTEST_TEST_CASE("build_options lists what CHANGE offers; build_commit replays the edits (WP3.4b)") {
    Set_Up_();
    static char design_name[] = "Warship";
    snprintf(c_players_[0].ship_designs[0].name, sizeof(c_players_[0].ship_designs[0].name), "%s", design_name);
    static char hull[] = "Cruiser";
    TECHDATA::hull_data[0].name = hull;
    static char part[] = "Part";
    TECHDATA::shields[0].name = part;
    TECHDATA::computers[0].name = part;
    TECHDATA::armor[0].name = part;
    TECHDATA::drives[0].name = part;
    c_players_[0].ship_designs[0].ship_weapon[0].type = 1;
    c_players_[0].ship_designs[0].ship_weapon[0].count = 4;
    COLONY::colony_handle = -7; // the popup's globals before: put back after
    const std::string options = Run_("{\"verb\":\"query\",\"what\":\"build_options\",\"colony\":0}");
    DOCTEST_CHECK(Has_(options, "\"buildings\":[{\"id\":-2,"));
    DOCTEST_CHECK(Has_(options, "{\"id\":22,\"name\":\"B22\""));
    DOCTEST_CHECK(Has_(options, "\"cost\":2200,\"turns\":7,\"upkeep\":"));
    DOCTEST_CHECK(Has_(options, "{\"separator\":true}"));
    DOCTEST_CHECK(Has_(options, "\"name\":\"Warship\""));
    DOCTEST_CHECK(Has_(options, "\"hull\":\"Cruiser\",\"shield\":\"Part\""));
    DOCTEST_CHECK(Has_(options, "\"weapons\":[{\"text\":\"4 Laser\"}]"));
    DOCTEST_CHECK(Has_(options, "\"queue\":[{\"id\":40,"));
    DOCTEST_CHECK(COLONY::colony_handle == -7);
    // The commit: the edits in order, quietly, with the colony's seed, then the popup's OK.
    TEST_COLONY::build_calls.clear();
    TEST_NATIVE::seeds_set.clear();
    TEST_NATIVE::random_seed = 1234;
    const std::string commit = Run_("{\"verb\":\"build_commit\",\"colony\":0,\"auto\":true,\"edits\":["
                                    "{\"add\":22},{\"add\":-50,\"repeat\":true},{\"move\":[1,0]},{\"delete\":2}]}");
    DOCTEST_CHECK(Has_(commit, "\"applied\":4"));
    DOCTEST_CHECK(TEST_COLONY::build_calls ==
        std::vector<std::string>{"add 22 quiet", "add -50 repeat quiet", "move 1 0", "delete 2", "commit quiet"});
    DOCTEST_CHECK(TEST_NATIVE::seeds_set == std::vector<uint32_t>{0, 1234}); // the colony's number, then back
    DOCTEST_CHECK(TEST_NATIVE::random_seed == 1234);
    DOCTEST_CHECK(c_colonies_[0].producing[0] == 22);
    DOCTEST_CHECK(c_colonies_[0].producing[1] == BUILDING_STAR_BASE);
    DOCTEST_CHECK(c_colonies_[0].auto_building == 1);
    DOCTEST_CHECK_FALSE(COLBLDG::quiet_ui);
    DOCTEST_CHECK(COLONY::colony_handle == -7);
}

DOCTEST_TEST_CASE("REFIT lists the warships in orbit and their designs; a refit edit queues the ship (WP3.4b)") {
    Set_Up_();
    s_ship_data* ships = c_ships_;
    ships[3].owner = 0;
    ships[3].status = 0;
    ships[3].location = 0;
    ships[3].d.ship_type = SHIP_TYPE_COMBAT;
    ships[3].d.size = 2;
    snprintf(ships[3].d.name, sizeof(ships[3].d.name), "Old Guard");
    ships[4] = ships[3];
    ships[4].location = 1; // elsewhere
    snprintf(c_players_[0].ship_designs[1].name, sizeof(c_players_[0].ship_designs[1].name), "New Guard");
    c_players_[0].ship_designs[1].size = 2;
    snprintf(c_players_[0].ship_designs[2].name, sizeof(c_players_[0].ship_designs[2].name), "Other Hull");
    c_players_[0].ship_designs[2].size = 3;
    static char hull[] = "Cruiser";
    for (auto& h : TECHDATA::hull_data) {
        h.name = hull;
    }
    // Without Chemistry and Nuclear Fission the button only says why.
    std::string options = Run_("{\"verb\":\"query\",\"what\":\"build_options\",\"colony\":0}");
    DOCTEST_CHECK(Has_(options, "\"refit\":{\"available\":false,\"message\":"));
    std::string commit = Run_("{\"verb\":\"build_commit\",\"colony\":0,\"edits\":[{\"refit\":3,\"design\":1}]}");
    DOCTEST_CHECK(Has_(commit, "\"applied\":0"));
    c_players_[0].tech_fields[TECH_FIELD_CHEMISTRY] = TECH_RESEARCH_STATUS_RESEARCHED;
    c_players_[0].tech_fields[TECH_FIELD_NUCLEAR_FISSION] = TECH_RESEARCH_STATUS_RESEARCHED;
    options = Run_("{\"verb\":\"query\",\"what\":\"build_options\",\"colony\":0}");
    DOCTEST_CHECK(Has_(options, "\"refit\":{\"available\":true,\"ships\":[{\"ship\":3,\"name\":\"Old Guard\""));
    DOCTEST_CHECK(Has_(options, "\"targets\":[{\"design\":1,\"name\":\"New Guard\",\"cost\":25}]"));
    DOCTEST_CHECK_FALSE(Has_(options, "\"ship\":4"));
    DOCTEST_CHECK(Has_(options, "\"slots\":7"));
    // A design of another size is refused, as the popup refuses it.
    commit = Run_("{\"verb\":\"build_commit\",\"colony\":0,\"edits\":[{\"refit\":3,\"design\":2},{\"refit\":3,\"design\":1}]}");
    DOCTEST_CHECK(Has_(commit, "\"applied\":1"));
    DOCTEST_CHECK(c_colonies_[0].producing[1] == COLBLDG::Colony_Production_From_Ship_Index(3));
    DOCTEST_CHECK(ships[3].status == 4);
    DOCTEST_CHECK(std::string(ships[3].d.name) == "New Guard");
    // A refit taken off again in the same commit puts the ship back (the popup would scrap it).
    ships[3].status = 0;
    snprintf(ships[3].d.name, sizeof(ships[3].d.name), "Old Guard");
    c_colonies_[0].producing[1] = COLONY_PRODUCTION_NONE;
    commit = Run_("{\"verb\":\"build_commit\",\"colony\":0,\"edits\":[{\"refit\":3,\"design\":1},{\"delete\":1}]}");
    DOCTEST_CHECK(Has_(commit, "\"applied\":2"));
    DOCTEST_CHECK(ships[3].status == 0);
    DOCTEST_CHECK(std::string(ships[3].d.name) == "Old Guard");
    DOCTEST_CHECK(c_colonies_[0].producing[1] == COLONY_PRODUCTION_NONE);
}

namespace TEST_NATIVE {
    extern int helps;
} // namespace TEST_NATIVE

DOCTEST_TEST_CASE("a cluster of colonists moves together; buildings sell with the game's question (WP3.4)") {
    Set_Up_();
    // The three farmers to industry: all move.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"move_colonist\",\"colony\":0,\"pops\":[0,1,2],\"job\":1}"), "\"count\":3"));
    for (int i = 0; i < 3; ++i) {
        DOCTEST_CHECK(((c_colonies_[0].pop[i] >> 7) & 3) == 1);
    }
    // Back to farming, refused after the first: the rest stay where they were, assigned.
    TEST_COLONY::refuse_after = 1;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"move_colonist\",\"colony\":0,\"pops\":[0,1,2],\"job\":0}"), "\"count\":1"));
    DOCTEST_CHECK(((c_colonies_[0].pop[0] >> 7) & 3) == 0);
    DOCTEST_CHECK(((c_colonies_[0].pop[1] >> 7) & 3) == 1);
    DOCTEST_CHECK((c_colonies_[0].pop[1] & 0x200) != 0);
    DOCTEST_CHECK(((c_colonies_[0].pop[2] >> 7) & 3) == 1);
    // A colonist of another kind in the list doesn't come along (only a cluster moves).
    TEST_COLONY::refuse_after = -1;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"move_colonist\",\"colony\":0,\"pops\":[1,4],\"job\":2}"), "\"count\":1"));
    DOCTEST_CHECK(((c_colonies_[0].pop[4] >> 7) & 3) == 2);
    // Sell: the game's question; Yes takes it away and pays half its cost.
    const int32_t bc = c_players_[0].bc;
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"sell_building\",\"colony\":0,\"building\":22}"), "\"sold\":true"));
    DOCTEST_CHECK(c_colonies_[0].buildings[BUILDING_MARINE_BARRACKS] == 0);
    DOCTEST_CHECK(c_players_[0].bc == bc + 150);
    // Not standing, or one that can't be scrapped: refused before any question.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"sell_building\",\"colony\":0,\"building\":22}"), "refused"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"sell_building\",\"colony\":0,\"building\":3}"), "refused"));
    DOCTEST_CHECK(TEST_COLONY::sells == 1);
}

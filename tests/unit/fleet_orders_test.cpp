#include "pch.h"

#include "antares/fleet_orders.h"
#include "antares/native.h"
#include "doctest/doctest.h"

#include <set>
#include <string>
#include <vector>

// WP3.3c: the fleet box's queries and orders (src/antares/fleet_orders.cpp) against the stand-ins
// in tests/fakes/fake_fleet_runtime.cpp. What the rules functions do is checked by the rules replay
// (fleet_move and colonize scenarios); these cover the arguments, the errors and what the commands
// hand to those functions.

namespace TEST_FLEET {
    extern std::set<int16_t> not_orderable;
    extern s_ship_move_info try_result;
    extern bool apply_answer;
    extern std::vector<int16_t> tried;
    extern std::vector<int16_t> applied;
    extern int16_t applied_star;
    extern bool colonize_flag;
    extern bool outpost_flag;
    extern bool troops_flag;
    extern bool engage_flag;
    extern bool antares_flag;
    extern int16_t flag_stack;
    extern std::vector<int16_t> colonies;
    extern std::vector<int16_t> outposts;
    extern std::vector<int16_t> pooled;
    extern std::vector<int16_t> deployed;
    extern int16_t user_box_answer;
    extern std::vector<int16_t> user_boxes;
    extern int box_data_calls;
    extern int vanished;
    extern int relocations;
    extern int16_t relocated_to;
} // namespace TEST_FLEET

namespace {
    s_player g_players_[2];
    s_star_data g_stars_[3];
    s_planet_data g_planets_[2];
    s_colony g_colonies_[1];
    s_ship_data g_ships_[5];
    s_leader_data g_leaders_[MAX_LEADERS];

    bool Contains_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(const std::string& json) {
        return antares::native::Run_Command_For_Test(SCREEN_MAIN, json);
    }

    void Ship_(int16_t id, int8_t owner, uint8_t type, int16_t star) {
        g_ships_[id] = s_ship_data{};
        g_ships_[id].owner = owner;
        g_ships_[id].d.ship_type = type;
        g_ships_[id].d.previous_owner = owner;
        g_ships_[id].location = star;
        g_ships_[id].x = g_stars_[star].x;
        g_ships_[id].y = g_stars_[star].y;
        g_ships_[id].officer_index = -1;
        snprintf(g_ships_[id].d.name, sizeof(g_ships_[id].d.name), "Ship%d", id);
    }

    // Star 0: the player's fleet (stack 0: ships 0 combat, 1 and 2 colony ships) and planets 0 (free)
    // and 1 (the player's colony 0). Star 1: the player's ship 3 (stack 1) and the other player's
    // ship 4 (stack 2). Star 2: a black hole.
    void Set_Up_() {
        for (auto& p : g_players_) {
            p = s_player{};
        }
        MOX::player = g_players_;
        MOX::NUM_PLAYERS = 2;
        MOX::PLAYER_NUM = 0;
        MOX::current_screen = SCREEN_MAIN;
        for (int i = 0; i < 3; ++i) {
            g_stars_[i] = s_star_data{};
            snprintf(g_stars_[i].name, sizeof(g_stars_[i].name), "Star%d", i);
            g_stars_[i].x = (int16_t)(100 * (i + 1));
            g_stars_[i].y = 50;
            for (auto& slot : g_stars_[i].planet_index) {
                slot = -1;
            }
        }
        g_stars_[2].spectral_class = STAR_CLASS_BLACK_HOLE;
        g_stars_[0].planet_index[0] = 0;
        g_stars_[0].planet_index[1] = 1;
        for (auto& planet : g_planets_) {
            planet = s_planet_data{};
            planet.planet_type = PLANET_TYPE_PLANET;
            planet.colony_index = -1;
        }
        g_planets_[1].colony_index = 0;
        g_colonies_[0] = s_colony{};
        g_colonies_[0].owner = 0;
        g_colonies_[0].planet = 1;
        MOX::star = g_stars_;
        MOX::NUM_STARS = 3;
        MOX::planet = g_planets_;
        MOX::planet_count = 2;
        MOX::colony = g_colonies_;
        MOX::NUM_COLONIES = 1;

        Ship_(0, 0, SHIP_TYPE_COMBAT, 0);
        Ship_(1, 0, SHIP_TYPE_COLONY, 0);
        Ship_(2, 0, SHIP_TYPE_COLONY, 0);
        Ship_(3, 0, SHIP_TYPE_COMBAT, 1);
        Ship_(4, 1, SHIP_TYPE_COMBAT, 1);
        MOX::ship = g_ships_;
        MOX::NUM_SHIPS = 5;
        const int16_t next[5] = {1, 2, -1, -1, -1};
        for (int16_t n = 0; n < 5; ++n) {
            MOX::ship_node[n] = s_ship_node{n, next[n], 0};
        }
        MOX::ship_stack_count = 3;
        MOX::ship_stack_start[0] = 0;
        MOX::ship_stack_start[1] = 3;
        MOX::ship_stack_start[2] = 4;
        MOX::ship_icon_count = 3;
        MOX::ship_icon[0].node_idx = 0;
        MOX::ship_icon[1].node_idx = 3;
        MOX::ship_icon[2].node_idx = 4;
        for (auto& leader : g_leaders_) {
            leader = s_leader_data{};
            leader.location = -1;
        }
        MOX::leaders = g_leaders_;
        MOX::moveable_box[0].id = -1;
        MOX::moveable_box[2].id = -1;
        MOX::fleet_box_ship_stack = -1;

        TEST_FLEET::not_orderable.clear();
        TEST_FLEET::try_result = s_ship_move_info{};
        TEST_FLEET::try_result.moving = 1;
        TEST_FLEET::try_result.turns_left = 2;
        TEST_FLEET::apply_answer = true;
        TEST_FLEET::tried.clear();
        TEST_FLEET::applied.clear();
        TEST_FLEET::applied_star = -1;
        TEST_FLEET::colonize_flag = TEST_FLEET::outpost_flag = TEST_FLEET::troops_flag = false;
        TEST_FLEET::engage_flag = TEST_FLEET::antares_flag = false;
        TEST_FLEET::flag_stack = -1;
        TEST_FLEET::colonies.clear();
        TEST_FLEET::outposts.clear();
        TEST_FLEET::pooled.clear();
        TEST_FLEET::deployed.clear();
        TEST_FLEET::user_box_answer = 1;
        TEST_FLEET::user_boxes.clear();
        TEST_FLEET::box_data_calls = 0;
        TEST_FLEET::vanished = 0;
        TEST_FLEET::relocations = 2;
        TEST_FLEET::relocated_to = -1;
    }

    void Tear_Down_() {
        MOX::player = nullptr;
        MOX::star = nullptr;
        MOX::NUM_STARS = 0;
        MOX::planet = nullptr;
        MOX::planet_count = 0;
        MOX::colony = nullptr;
        MOX::NUM_COLONIES = 0;
        MOX::ship = nullptr;
        MOX::NUM_SHIPS = 0;
        MOX::ship_stack_count = 0;
        MOX::ship_icon_count = 0;
        MOX::leaders = nullptr;
    }
} // namespace

DOCTEST_TEST_CASE("move checks the ships and gives them in the fleet's order") {
    Set_Up_();
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[],"star":1})"), "bad_args"));
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[9],"star":1})"), "bad_args"));
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[1,1],"star":1})"), "bad_args"));
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[1],"star":7})"), "bad_args"));
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[4],"star":0})"), "refused"));    // not ours
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[1,3],"star":2})"), "refused"));  // two fleets
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[1],"star":2})"), "refused"));    // black hole
    TEST_FLEET::not_orderable = {1};
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[1],"star":1})"), "refused"));
    DOCTEST_CHECK(TEST_FLEET::applied.empty());

    TEST_FLEET::not_orderable.clear();
    const std::string result = Run_(R"({"verb":"move","ships":[2,1],"star":1})");
    DOCTEST_CHECK(Contains_(result, R"("done":true)"));
    DOCTEST_CHECK(TEST_FLEET::tried == std::vector<int16_t>{1, 2});
    DOCTEST_CHECK(TEST_FLEET::applied == std::vector<int16_t>{1, 2});
    DOCTEST_CHECK(TEST_FLEET::applied_star == 1);
    DOCTEST_CHECK(MOX::just_moved_stack == 0); // a split: the box keeps the ship left behind
    Tear_Down_();
}

DOCTEST_TEST_CASE("move reports a blocked route and a No as not done") {
    Set_Up_();
    TEST_FLEET::try_result = s_ship_move_info{};
    TEST_FLEET::try_result.blackhole_blocks = 1;
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[0,1,2],"star":1})"), R"("done":false)"));
    DOCTEST_CHECK(TEST_FLEET::user_boxes == std::vector<int16_t>{3}); // the legacy warning
    DOCTEST_CHECK(TEST_FLEET::applied.empty());

    TEST_FLEET::try_result.blackhole_blocks = 0;
    TEST_FLEET::try_result.moving = 1;
    TEST_FLEET::apply_answer = false; // the fleet-split question answered No
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"move","ships":[0,1,2],"star":1})"), R"("done":false)"));
    Tear_Down_();
}

DOCTEST_TEST_CASE("the fleet query lists the stack and asks the button rules for that stack") {
    Set_Up_();
    TEST_FLEET::colonize_flag = true;
    const std::string result = Run_(R"({"verb":"query","what":"fleet","ship":2})");
    DOCTEST_CHECK(Contains_(result, R"("stack":0)"));
    DOCTEST_CHECK(Contains_(result, R"("ships":[{"id":0)"));
    DOCTEST_CHECK(Contains_(result, R"("colonize":true)"));
    DOCTEST_CHECK(Contains_(result, R"("colonize":[{"planet":0)")); // planet 1 is a colony already
    DOCTEST_CHECK(TEST_FLEET::flag_stack == 0);
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"query","what":"next_fleet","fleet":0,"dir":0})"), R"("ship":3)"));
    Tear_Down_();
}

DOCTEST_TEST_CASE("colonize uses the fleet's first colony ship and returns its officer to the pool") {
    Set_Up_();
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"colonize","fleet":0,"planet":0})"), "refused")); // no button
    TEST_FLEET::colonize_flag = true;
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"colonize","fleet":0,"planet":1})"), "refused")); // a colony
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"colonize","fleet":4,"planet":0})"), "refused")); // not ours
    g_ships_[1].officer_index = 7;
    g_leaders_[7].type = LEADER_TYPE_SHIP;
    g_leaders_[7].location = 1;
    g_leaders_[7].status = 1;
    const std::string result = Run_(R"({"verb":"colonize","fleet":2,"planet":0})");
    DOCTEST_CHECK(Contains_(result, R"("done":true)"));
    DOCTEST_CHECK(Contains_(result, R"("ship":1)"));
    DOCTEST_CHECK(TEST_FLEET::colonies == std::vector<int16_t>{0});
    DOCTEST_CHECK(g_ships_[1].status == 5);
    DOCTEST_CHECK(TEST_FLEET::pooled == std::vector<int16_t>{7});
    Tear_Down_();
}

DOCTEST_TEST_CASE("the consume step pools an officer aboard or on the way, not others") {
    Set_Up_();
    g_ships_[2].officer_index = 3;
    g_leaders_[3].type = LEADER_TYPE_SHIP;
    g_leaders_[3].location = 2;
    g_leaders_[4].type = LEADER_TYPE_SHIP; // on the way to ship 2
    g_leaders_[4].location = 2;
    g_leaders_[4].eta = 2;
    g_leaders_[5].type = LEADER_TYPE_COLONY; // a colony leader at star 2: not the ship's
    g_leaders_[5].location = 2;
    antares::fleet::Consume_Ship(2);
    DOCTEST_CHECK(TEST_FLEET::pooled == std::vector<int16_t>{3, 4});
    DOCTEST_CHECK(g_ships_[2].status == 5);
    Tear_Down_();
}

DOCTEST_TEST_CASE("open_fleet keeps the saved box record and sizes it") {
    Set_Up_();
    MOX::moveable_box[0].id = 1; // the system box is open: opening a fleet closes it
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"open_fleet","fleet":3})"), R"("ship":3)"));
    DOCTEST_CHECK(MOX::moveable_box[2].id == 1);
    DOCTEST_CHECK(MOX::fleet_box_ship_stack == 1);
    DOCTEST_CHECK(TEST_FLEET::box_data_calls == 1);
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"close_fleet"})"), R"("ok":true)"));
    DOCTEST_CHECK(TEST_FLEET::vanished == 1);
    Tear_Down_();
}

DOCTEST_TEST_CASE("attack_antares asks first and deploys the listed ships") {
    Set_Up_();
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"attack_antares","ships":[0]})"), "refused")); // no portal
    TEST_FLEET::antares_flag = true;
    TEST_FLEET::user_box_answer = 0;
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"attack_antares","ships":[0]})"), R"("done":false)"));
    DOCTEST_CHECK(TEST_FLEET::deployed.empty());
    TEST_FLEET::user_box_answer = 1;
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"attack_antares","ships":[2,0]})"), R"("done":true)"));
    DOCTEST_CHECK(TEST_FLEET::deployed == std::vector<int16_t>{0, 2});
    Tear_Down_();
}

DOCTEST_TEST_CASE("merge_relocations checks the count as F4 does, then merges") {
    Set_Up_();
    TEST_FLEET::relocations = 1;
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"merge_relocations"})"), R"("ready":false)"));
    DOCTEST_CHECK(TEST_FLEET::user_boxes.size() == 1);
    TEST_FLEET::relocations = 3;
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"merge_relocations"})"), R"("ready":true)"));
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"merge_relocations","star":2})"), "refused")); // black hole
    DOCTEST_CHECK(Contains_(Run_(R"({"verb":"merge_relocations","star":1})"), R"("done":true)"));
    DOCTEST_CHECK(TEST_FLEET::relocated_to == 1);
    Tear_Down_();
}

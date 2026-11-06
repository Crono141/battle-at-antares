#include "pch.h"

#include "antares/commands.h"
#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>
#include <vector>

namespace TEST_NATIVE {
    extern std::vector<int16_t> scrapped;
    extern int8_t scrap_answer;
    extern int icon_rebuilds;
} // namespace TEST_NATIVE

// WP3.1b and WP3.2b: the native command bridge (src/antares/native.cpp) and the command table
// (src/antares/commands.cpp). The loop itself needs a running game and a frontend; it is checked
// by the Godot smoke test and the rules replay (notes/antares/native-bridge.md). These cover the
// interpreter, the error codes of the contract, the read-only guard, snapshots and the claim rules.

namespace {
    bool Contains_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }
} // namespace

DOCTEST_TEST_CASE("native commands reject what they can't run") {
    using antares::native::Run_Command_For_Test;
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "not json"), "\"error\":\"bad_json\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "[1,2]"), "\"error\":\"bad_json\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"fly\"}"), "\"error\":\"unknown_verb\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_SHOW_COMMAND_POINTS, "{\"verb\":\"end_turn\"}"),
        "\"error\":\"wrong_screen\""));
    // Only the galaxy's launchers can be opened; screens that need setup the bridge doesn't do
    // (a colony, ship design) are refused rather than opened half set up.
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"open\",\"screen\":1}"), "\"error\":\"refused\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"open\"}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"weather\"}"),
        "\"error\":\"unknown_query\""));
    // Every error carries a readable message too.
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"fly\"}"), "\"message\":"));
}

DOCTEST_TEST_CASE("open and close move between screens as the legacy galaxy does") {
    using antares::native::Run_Command_For_Test;
    const int16_t saved_current = MOX::current_screen;
    const int16_t saved_return = MOX::return_screen;

    MOX::current_screen = SCREEN_MAIN;
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"open\",\"screen\":20}"), "\"ok\":true"));
    DOCTEST_CHECK(MOX::current_screen == SCREEN_COLONY_SUMMARY);
    DOCTEST_CHECK(MOX::return_screen == SCREEN_MAIN);

    MOX::current_screen = SCREEN_SHOW_COMMAND_POINTS;
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_SHOW_COMMAND_POINTS, "{\"verb\":\"close\"}"), "\"ok\":true"));
    DOCTEST_CHECK(MOX::current_screen == SCREEN_MAIN);

    MOX::current_screen = saved_current;
    MOX::return_screen = saved_return;
}

DOCTEST_TEST_CASE("without an active host nothing is native") {
    // The SDL build never activates the host: the dispatcher, the legacy galaxy and User_Box
    // behave exactly as before even when a screen is claimed.
    antares::native::Claim_Screen(SCREEN_MAIN, true);
    antares::native::Claim_Requests({"message", "confirm", "warning", "choose"});
    DOCTEST_CHECK_FALSE(antares::native::Wants_Screen(SCREEN_MAIN));
    DOCTEST_CHECK_FALSE(antares::native::Run_Screen(SCREEN_MAIN));
    DOCTEST_CHECK_FALSE(antares::native::Message(nullptr, "hello", 0));
    DOCTEST_CHECK(antares::native::Native_Screen() == -1);
    antares::native::Claim_Screen(SCREEN_MAIN, false);
    antares::native::Claim_Requests({});
}

DOCTEST_TEST_CASE("command ids count up and results queue in order") {
    std::string json;
    while (antares::native::Take_Result(&json)) {
    }
    const uint64_t first = antares::native::Send_Command("{\"verb\":\"close\"}");
    const uint64_t second = antares::native::Send_Command("{\"verb\":\"close\"}");
    DOCTEST_CHECK(second == first + 1);
    DOCTEST_CHECK_FALSE(antares::native::Take_Result(&json)); // nothing runs without the loop
}

namespace {
    s_player g_players_[2];
    s_ship_data g_ships_[4];

    // A loaded game with four ships: 0 and 1 the player's at one place, 2 another player's,
    // 3 the player's but dead.
    void Set_Up_Ships_() {
        MOX::player = g_players_;
        MOX::NUM_PLAYERS = 2;
        MOX::PLAYER_NUM = 0;
        MOX::ship = g_ships_;
        MOX::NUM_SHIPS = 4;
        for (int i = 0; i < 4; ++i) {
            g_ships_[i] = s_ship_data{};
            g_ships_[i].owner = 0;
            g_ships_[i].status = 0;
            g_ships_[i].x = 100;
            g_ships_[i].y = 50;
            g_ships_[i].d.cost = (int16_t)(10 * (i + 1));
        }
        g_ships_[2].owner = 1;
        g_ships_[3].status = 5;
        TEST_NATIVE::scrapped.clear();
        TEST_NATIVE::scrap_answer = 1;
        TEST_NATIVE::icon_rebuilds = 0;
    }

    void Tear_Down_Ships_() {
        MOX::player = nullptr;
        MOX::ship = nullptr;
        MOX::NUM_SHIPS = 0;
    }

    int g_writes_ = 0;
    uint64_t Probe_() {
        return (uint64_t)g_writes_;
    }
    void Writing_Query_(int16_t, const antares::commands::s_args&, antares::commands::s_out& out) {
        ++g_writes_;
        out.Int("x", 1);
    }
    void Reading_Query_(int16_t, const antares::commands::s_args&, antares::commands::s_out& out) {
        out.Int("writes", g_writes_);
    }
} // namespace

DOCTEST_TEST_CASE("scrap_ships takes plain ship ids and checks them") {
    using antares::native::Run_Command_For_Test;
    Set_Up_Ships_();
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\"}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[]}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[9]}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[0,0]}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[0,\"1\"]}"), "\"error\":\"bad_args\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[2]}"), "\"error\":\"refused\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[3]}"), "\"error\":\"refused\""));
    g_ships_[1].x = 101;
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[0,1]}"), "\"error\":\"refused\""));
    DOCTEST_CHECK(TEST_NATIVE::scrapped.empty()); // nothing reached the rules
    g_ships_[1].x = 100;

    // The rules function gets exactly the listed ships, in order, and the galaxy's fleet icons
    // are rebuilt after a scrap.
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_FLEET, "{\"verb\":\"scrap_ships\",\"ships\":[1,0]}"), "\"done\":true"));
    DOCTEST_CHECK(TEST_NATIVE::scrapped == std::vector<int16_t>{1, 0});
    DOCTEST_CHECK(TEST_NATIVE::icon_rebuilds == 1);

    // The player said No to one of the questions: not an error, just not done.
    TEST_NATIVE::scrap_answer = 0;
    const std::string declined = Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"scrap_ships\",\"ships\":[0]}");
    DOCTEST_CHECK(Contains_(declined, "\"ok\":true"));
    DOCTEST_CHECK(Contains_(declined, "\"done\":false"));
    DOCTEST_CHECK(TEST_NATIVE::icon_rebuilds == 1);
    Tear_Down_Ships_();
}

DOCTEST_TEST_CASE("the ships query lists the player's live ships with their scrap value") {
    using antares::native::Run_Command_For_Test;
    Set_Up_Ships_();
    const std::string result = Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"ships\"}");
    DOCTEST_CHECK(Contains_(result, "\"ok\":true"));
    DOCTEST_CHECK(Contains_(result, "\"version\":1"));
    DOCTEST_CHECK(Contains_(result, "\"id\":0"));
    DOCTEST_CHECK(Contains_(result, "\"scrap_value\":20"));
    DOCTEST_CHECK_FALSE(Contains_(result, "\"scrap_value\":30")); // another player's
    DOCTEST_CHECK_FALSE(Contains_(result, "\"scrap_value\":40")); // dead
    Tear_Down_Ships_();
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"ships\"}"),
        "\"error\":\"no_game\""));
}

DOCTEST_TEST_CASE("the read-only guard catches a query that changes the game") {
    using namespace antares::commands;
    Register_For_Test({"test_writes", e_kind::query, ANY_SCREEN, 1, "", Writing_Query_});
    Register_For_Test({"test_reads", e_kind::query, ANY_SCREEN, 1, "", Reading_Query_});
    Set_State_Probe_For_Test(Probe_);
    Set_Guard_For_Test(true);
    const uint64_t before = Guard_Violations();

    DOCTEST_CHECK(Contains_(Run(SCREEN_MAIN, 1, "{\"verb\":\"query\",\"what\":\"test_reads\"}"), "\"ok\":true"));
    DOCTEST_CHECK(Guard_Violations() == before);
    const std::string caught = Run(SCREEN_MAIN, 2, "{\"verb\":\"query\",\"what\":\"test_writes\"}");
    DOCTEST_CHECK(Contains_(caught, "\"ok\":false"));
    DOCTEST_CHECK(Contains_(caught, "\"error\":\"query_wrote_state\""));
    DOCTEST_CHECK(Guard_Violations() == before + 1);

    // Off (release builds without ANTARES_QUERY_GUARD), the same query isn't checked.
    Set_Guard_For_Test(false);
    DOCTEST_CHECK(Contains_(Run(SCREEN_MAIN, 3, "{\"verb\":\"query\",\"what\":\"test_writes\"}"), "\"ok\":true"));
    DOCTEST_CHECK(Guard_Violations() == before + 1);
    Set_State_Probe_For_Test(nullptr);

    // Every query in the real table is read-only by kind; commands are never guarded.
    for (const s_entry& entry : Table()) {
        DOCTEST_CHECK(entry.run != nullptr);
        DOCTEST_CHECK(entry.name[0] != '\0');
    }
}

DOCTEST_TEST_CASE("a watched query becomes a snapshot each time its answer changes") {
    using antares::native::Run_Command_For_Test;
    std::string json;
    while (antares::native::Take_Snapshot(&json)) {
    }
    antares::commands::Register_For_Test({"test_reads", antares::commands::e_kind::query, antares::commands::ANY_SCREEN, 1, "", Reading_Query_});
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"watch\",\"what\":\"nothing\"}"),
        "\"error\":\"unknown_query\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"watch\",\"what\":\"test_reads\"}"), "\"ok\":true"));

    antares::native::Refresh_Snapshots_For_Test(SCREEN_MAIN);
    DOCTEST_REQUIRE(antares::native::Take_Snapshot(&json));
    DOCTEST_CHECK(Contains_(json, "\"snapshot\":\"test_reads\""));
    DOCTEST_CHECK(Contains_(json, "\"version\":1"));
    antares::native::Refresh_Snapshots_For_Test(SCREEN_MAIN);
    DOCTEST_CHECK_FALSE(antares::native::Take_Snapshot(&json)); // unchanged: nothing posted

    ++g_writes_;
    antares::native::Refresh_Snapshots_For_Test(SCREEN_MAIN);
    DOCTEST_CHECK(antares::native::Take_Snapshot(&json));

    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"unwatch\",\"what\":\"test_reads\"}"), "\"ok\":true"));
    ++g_writes_;
    antares::native::Refresh_Snapshots_For_Test(SCREEN_MAIN);
    DOCTEST_CHECK_FALSE(antares::native::Take_Snapshot(&json));
}

DOCTEST_TEST_CASE("requests stay legacy without a frontend or an unclaimed kind") {
    int16_t answer = 7;
    antares::native::Claim_Requests({"message", "confirm"});
    // No host and no control channel: every dialog keeps its legacy popup.
    DOCTEST_CHECK_FALSE(antares::native::Message("Title", "text", 0));
    DOCTEST_CHECK_FALSE(antares::native::Confirm("sure?", &answer));
    DOCTEST_CHECK(answer == 7);
    // Attached, a kind that wasn't claimed still falls back.
    antares::native::Attach_Control(true);
    DOCTEST_CHECK_FALSE(antares::native::Warning("careful"));
    const char list[3][8] = {"one", "two", ""};
    int8_t selected = 0;
    DOCTEST_CHECK_FALSE(antares::native::Choose("pick", &list[0][0], 8, nullptr, &selected, &answer));
    antares::native::Attach_Control(false);
    antares::native::Claim_Requests({});
}

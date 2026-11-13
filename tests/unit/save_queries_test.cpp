#include "pch.h"

#include "antares/native.h"

#include <doctest/doctest.h>

#include <string>

// The native game menu's Load and Save (src/antares/save_queries.cpp), against the fakes in
// tests/fakes/fake_save_runtime.cpp: a slot table in memory instead of SAVE*.GAM files.

namespace TEST_SAVE {
    extern int saves;
    extern int loads;
    extern int16_t last_slot;
    extern bool load_ok;
    void Reset();
    void Put(int16_t slot, int32_t type, const char* description, int32_t stardate);
} // namespace TEST_SAVE

namespace {
    std::string Run_(int16_t screen, const std::string& json) {
        return antares::native::Run_Command_For_Test(screen, json);
    }
    bool Has_(const std::string& text, const std::string& part) {
        return text.find(part) != std::string::npos;
    }
} // namespace

DOCTEST_TEST_CASE("the saves query lists the ten slots; save_game and load_game use them") {
    static s_player players[1] = {};
    s_player* const saved_players = MOX::player;
    const int16_t saved_count = MOX::NUM_PLAYERS;
    MOX::player = players;
    MOX::NUM_PLAYERS = 1;
    MOX::PLAYER_NUM = 0;
    strlcpy(players[0].name, "Ruler", sizeof(players[0].name));
    TEST_SAVE::Reset();
    TEST_SAVE::Put(0, 0, "Mine", 35123);
    TEST_SAVE::Put(2, 3, "Net game", 35010);
    TEST_SAVE::Put(9, 0, "whatever", 35200);
    const std::string saves = Run_(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"saves\"}");
    DOCTEST_CHECK(Has_(saves, "{\"slot\":0,\"state\":\"ok\",\"description\":\"Mine\",\"stardate\":35123"));
    DOCTEST_CHECK(Has_(saves, "{\"slot\":1,\"state\":\"empty\"}"));
    DOCTEST_CHECK(Has_(saves, "{\"slot\":2,\"state\":\"network\""));
    DOCTEST_CHECK(Has_(saves, "{\"slot\":9,\"state\":\"ok\",\"description\":\"(Auto Save)\""));

    DOCTEST_CHECK(Has_(Run_(SCREEN_MAIN, "{\"verb\":\"save_game\",\"slot\":10}"), "bad_args"));
    const std::string saved = Run_(SCREEN_MAIN, "{\"verb\":\"save_game\",\"slot\":1,\"description\":\"Before \\u00e9 the war\"}");
    DOCTEST_CHECK(Has_(saved, "\"saved\":true"));
    DOCTEST_CHECK(Has_(saved, "\"description\":\"Before  the war\"")); // printable ASCII only
    DOCTEST_CHECK(TEST_SAVE::saves == 1);
    DOCTEST_CHECK(TEST_SAVE::last_slot == 1);
    DOCTEST_CHECK(MOX::settings.active_save_slot == 1);

    MOX::disallow_saves_code = 1;
    DOCTEST_CHECK(Has_(Run_(SCREEN_MAIN, "{\"verb\":\"save_game\",\"slot\":1}"), "refused"));
    MOX::disallow_saves_code = 0;

    DOCTEST_CHECK(Has_(Run_(SCREEN_MAIN, "{\"verb\":\"load_game\",\"slot\":4}"), "refused")); // empty
    DOCTEST_CHECK(Has_(Run_(SCREEN_MAIN, "{\"verb\":\"load_game\",\"slot\":2}"), "refused")); // network
    MOX::current_screen = SCREEN_MAIN;
    DOCTEST_CHECK(Has_(Run_(SCREEN_MAIN, "{\"verb\":\"load_game\",\"slot\":0}"), "\"loaded\":true"));
    DOCTEST_CHECK(TEST_SAVE::loads == 1);
    DOCTEST_CHECK(MOX::current_screen == SCREEN_REPORTS);
    DOCTEST_CHECK(MOX::settings.active_save_slot == 0);
    MOX::current_screen = SCREEN_MAIN;
    MOX::player = saved_players;
    MOX::NUM_PLAYERS = saved_count;
}

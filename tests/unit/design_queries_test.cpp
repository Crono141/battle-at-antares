#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.6: the Ship Design panel's commands (src/antares/design_queries.cpp) on the fakes' small
// design model (tests/fakes/fake_design_runtime.cpp): a 31-space hull, weapons 3 space a unit,
// specials 5.

namespace {
    s_player d_players_[2];

    bool Has_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }

    std::string Run_(const std::string& json) {
        return antares::native::Run_Command_For_Test(SCREEN_MAIN, json);
    }
} // namespace

DOCTEST_TEST_CASE("the Ship Design panel: open, weapons, specials, refusals, build (WP3.6)") {
    for (auto& p : d_players_) {
        p = s_player{};
    }
    MOX::player = d_players_;
    MOX::NUM_PLAYERS = 2;
    MOX::PLAYER_NUM = 0;
    MOX::current_screen = SCREEN_MAIN;
    strlcpy(d_players_[0].ship_designs[2].name, "Scout", sizeof(d_players_[0].ship_designs[2].name));
    s_current_design* legacy = MOX::design;

    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"design\"}"), "refused")); // nothing open
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_open\",\"slot\":9}"), "bad_args"));
    const std::string opened = Run_("{\"verb\":\"design_open\",\"slot\":2}");
    DOCTEST_CHECK(Has_(opened, "\"name\":\"Scout\""));
    DOCTEST_CHECK(Has_(opened, "\"space_used\":3"));
    DOCTEST_CHECK(MOX::design == legacy); // the game's pointer is put back after every call

    // A missile on a new row (arc forced to 360), one more of it; an unresearched weapon refused.
    const std::string missile = Run_("{\"verb\":\"design_weapon\",\"slot\":-1,\"weapon\":3,\"arc\":0}");
    DOCTEST_CHECK(Has_(missile, "\"slot\":1,\"weapon\":3"));
    DOCTEST_CHECK(Has_(missile, "\"arc\":4"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_weapon_count\",\"slot\":1,\"delta\":1}"), "\"space_used\":9"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_weapon\",\"slot\":-1,\"weapon\":2}"), "refused"));
    // Mods: Heavy Mount with Point Defense refused; a beam with the back arc and Point Defense.
    char json[200];
    snprintf(json, sizeof(json), "{\"verb\":\"design_weapon\",\"slot\":0,\"weapon\":1,\"arc\":3,\"mods\":[%d,%d]}", WEAPON_MOD_HEAVY_MOUNT, WEAPON_MOD_POINT_DEFENSE);
    DOCTEST_CHECK(Has_(Run_(json), "exclude each other"));
    snprintf(json, sizeof(json), "{\"verb\":\"design_weapon\",\"slot\":0,\"weapon\":1,\"arc\":3,\"mods\":[%d]}", WEAPON_MOD_POINT_DEFENSE);
    DOCTEST_CHECK(Has_(Run_(json), "\"slot\":0,\"weapon\":1"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"design\"}"), "\"arc\":3"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"design_weapons\"}"), "\"category\":\"missile\""));

    // Specials: one fitted, again refused, its excluded partner refused; removed.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_special\",\"slot\":-1,\"special\":2}"), "\"special\":2"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_special\",\"slot\":-1,\"special\":2}"), "fitted already"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_special\",\"slot\":-1,\"special\":3}"), "can't go with"));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"design_specials\"}"), "\"fitted\":true"));
    DOCTEST_CHECK(!Has_(Run_("{\"verb\":\"design_special\",\"slot\":0,\"special\":0}"), "\"special\":2"));

    // Names: a typed one sticks through a picture change; the size button names it anew.
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_edit\",\"name\":\"  Lancer  \"}"), "\"name\":\"Lancer\""));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_edit\",\"picture\":1}"), "\"name\":\"Lancer\""));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_edit\",\"size\":1}"), "\"name\":\"Hull 1-0\""));
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"design_edit\",\"name\":\"Lancer\"}"), "\"name\":\"Lancer\""));

    // Too full: BUILD refused until it fits (the fake hull holds 31: ten units are 30, then 33).
    Run_("{\"verb\":\"design_clear\"}");
    Run_("{\"verb\":\"design_weapon\",\"slot\":-1,\"weapon\":1}");
    for (int i = 0; i < 12; ++i) {
        Run_("{\"verb\":\"design_weapon_count\",\"slot\":0,\"delta\":1}");
    }
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"design\"}"), "\"space_used\":30"));
    const std::string built = Run_("{\"verb\":\"design_build\"}");
    DOCTEST_CHECK(Has_(built, "\"built\":true"));
    DOCTEST_CHECK(std::string(d_players_[0].ship_designs[2].name) == "Lancer");
    DOCTEST_CHECK(Has_(Run_("{\"verb\":\"query\",\"what\":\"design\"}"), "refused")); // closed by BUILD
    DOCTEST_CHECK(MOX::design == legacy);
}

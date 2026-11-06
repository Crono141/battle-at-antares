#include "pch.h"

#include "antares/native.h"
#include "doctest/doctest.h"

#include <string>

// WP3.1b: the native command bridge (src/antares/native.cpp). The loop itself needs a running
// game and a frontend; it is checked by the Godot smoke test and the rules replay
// (notes/antares/native-bridge.md). These cover the command interpreter and the claim rules.

namespace {
    bool Contains_(const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    }
} // namespace

DOCTEST_TEST_CASE("native commands reject what they can't run") {
    using antares::native::Run_Command_For_Test;
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "not json"), "\"error\":\"bad json\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"fly\"}"), "\"error\":\"unknown verb\""));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_SHOW_COMMAND_POINTS, "{\"verb\":\"end_turn\"}"),
        "\"error\":\"end_turn needs the galaxy\""));
    // Only the galaxy's launchers can be opened; screens that need setup the bridge doesn't do
    // (a colony, ship design) are refused rather than opened half set up.
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"open\",\"screen\":1}"), "\"ok\":false"));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"open\"}"), "\"ok\":false"));
    DOCTEST_CHECK(Contains_(Run_Command_For_Test(SCREEN_MAIN, "{\"verb\":\"query\",\"what\":\"weather\"}"), "\"ok\":false"));
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
    antares::native::Claim_Requests(true);
    DOCTEST_CHECK_FALSE(antares::native::Wants_Screen(SCREEN_MAIN));
    DOCTEST_CHECK_FALSE(antares::native::Run_Screen(SCREEN_MAIN));
    int16_t result = 7;
    DOCTEST_CHECK_FALSE(antares::native::User_Box(0, "hello", &result));
    DOCTEST_CHECK(result == 7);
    DOCTEST_CHECK(antares::native::Native_Screen() == -1);
    antares::native::Claim_Screen(SCREEN_MAIN, false);
    antares::native::Claim_Requests(false);
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

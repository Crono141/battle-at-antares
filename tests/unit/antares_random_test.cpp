#include "pch.h"

#include "antares/cosmetic_random.h"
#include "antares/rng_trace.h"
#include "doctest/doctest.h"

DOCTEST_TEST_CASE("cosmetic draws leave the rules generator alone") {
    game_random::Set_Random_Seed(12345);
    const uint64_t calls = antares::rng_trace::g_calls;
    for (int i = 0; i < 1000; ++i) {
        const uint32_t value = antares::cosmetic_random::Random(7);
        DOCTEST_CHECK(value >= 1);
        DOCTEST_CHECK(value <= 7);
    }
    DOCTEST_CHECK(game_random::Get_Random_Seed() == 12345);
    DOCTEST_CHECK(antares::rng_trace::g_calls == calls);
}

DOCTEST_TEST_CASE("cosmetic generator keeps game_random's contract") {
    DOCTEST_CHECK(antares::cosmetic_random::Random(0) == 1);
    DOCTEST_CHECK(antares::cosmetic_random::Random(1) == 1);
    antares::cosmetic_random::Seed(42);
    bool seen[4] = {};
    for (int i = 0; i < 200; ++i) {
        seen[antares::cosmetic_random::Random(3)] = true;
    }
    DOCTEST_CHECK((seen[1] && seen[2] && seen[3]));
}

DOCTEST_TEST_CASE("the rules generator counts its calls for the replay trace") {
    const uint64_t calls = antares::rng_trace::g_calls;
    game_random::Random(10);
    game_random::Random(0);
    DOCTEST_CHECK(antares::rng_trace::g_calls == calls + 2);
}

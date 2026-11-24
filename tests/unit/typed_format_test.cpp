#include "pch.h"

#include "doctest/doctest.h"
#include "game/typed_format.h"

#include <cstring>

namespace {
    std::string Run(const char* format) {
        const int numbers[] = {5};
        const char* texts[] = {"Admiral Sparky", "Tarantula"};
        char out[200];
        TYPED_FORMAT::Format(out, sizeof(out), format, numbers, 1, texts, 2);
        return out;
    }
}

DOCTEST_TEST_CASE("typed format: the English order") {
    DOCTEST_CHECK(Run("It will take %d turns for %s to arrive on %s, okay?") ==
        "It will take 5 turns for Admiral Sparky to arrive on Tarantula, okay?");
}

DOCTEST_TEST_CASE("typed format: a translation with the number elsewhere") {
    // As snprintf with the English arguments this read 5 as a text pointer (signal 11).
    DOCTEST_CHECK(Run("%s braucht %d Runden bis zur %s, okay?") == "Admiral Sparky braucht 5 Runden bis zur Tarantula, okay?");
    DOCTEST_CHECK(Run("%s, %s: %d") == "Admiral Sparky, Tarantula: 5");
}

DOCTEST_TEST_CASE("typed format: too few arguments, widths, percent signs, truncation") {
    DOCTEST_CHECK(Run("%s %s %s %d %d") == "Admiral Sparky Tarantula ? 5 ?");
    DOCTEST_CHECK(Run("[%3d] 100%% %-5s|") == "[  5] 100% Admiral Sparky|");
    DOCTEST_CHECK(Run("%ld %x") == "5 %x");
    char small[8];
    const int numbers[] = {12345};
    TYPED_FORMAT::Format(small, sizeof(small), "abc %d def", numbers, 1, nullptr, 0);
    DOCTEST_CHECK(std::strcmp(small, "abc 123") == 0);
}

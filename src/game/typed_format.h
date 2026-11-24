#pragma once

#include <cstddef>

// Battle at Antares: a game message's printf format filled by type, not by position.
//
// The game's translated strings (HESTRNGS and its German, French, Spanish and Italian files) do
// not keep the English order of their numbers and names. The original code swapped them for one
// language (settings.language == 4) and passed the English order to the rest; a translation with
// "%s" where English has "%d" handed snprintf a number as a text pointer (a reported crash,
// 2026-11-23: assigning a ship officer, signal 11, a German game). Here every "%d"/"%i"/"%u"
// takes the next number and every "%s" the next text, in the order each kind is given, so any
// translation that keeps the names' order among themselves is safe. A conversion with nothing
// left of its kind prints "?", and conversions other than these are copied as written.
namespace TYPED_FORMAT {
    void Format(char* out, size_t capacity, const char* format, const int* numbers, size_t number_count,
        const char* const* texts, size_t text_count);
}

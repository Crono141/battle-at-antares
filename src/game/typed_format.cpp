#include "pch.h"

#include "typed_format.h"

#include <cstdio>
#include <cstring>

namespace TYPED_FORMAT {
    void Format(char* out, size_t capacity, const char* format, const int* numbers, size_t number_count,
        const char* const* texts, size_t text_count) {
        if (out == nullptr || capacity == 0) {
            return;
        }
        size_t at = 0;
        auto put = [&](const char* text, size_t length) {
            const size_t room = capacity - 1 - at;
            const size_t n = length < room ? length : room;
            memcpy(out + at, text, n);
            at += n;
        };
        size_t next_number = 0;
        size_t next_text = 0;
        const char* p = format != nullptr ? format : "";
        while (*p != '\0') {
            if (*p != '%') {
                put(p, 1);
                ++p;
                continue;
            }
            if (p[1] == '%') {
                put("%", 1);
                p += 2;
                continue;
            }
            // %[flags][width][.precision][length]type
            const char* spec = p + 1;
            while (*spec != '\0' && strchr("-+ #0", *spec) != nullptr) ++spec;
            while (*spec >= '0' && *spec <= '9') ++spec;
            if (*spec == '.') {
                ++spec;
                while (*spec >= '0' && *spec <= '9') ++spec;
            }
            while (*spec == 'h' || *spec == 'l') ++spec;
            const char type = *spec;
            if (type == '\0') {
                put(p, strlen(p));
                break;
            }
            const size_t spec_length = (size_t)(spec - p) + 1;
            char one[32];
            char piece[256];
            if (spec_length >= sizeof(one)) {
                put(p, spec_length);
            } else if (type == 'd' || type == 'i' || type == 'u') {
                if (next_number < number_count) {
                    // Rebuilt as "%<flags,width,precision>d": the length modifiers dropped.
                    size_t k = 0;
                    for (const char* c = p; c < spec; ++c) {
                        if (*c != 'h' && *c != 'l') one[k++] = *c;
                    }
                    one[k++] = type == 'u' ? 'u' : 'd';
                    one[k] = '\0';
                    const int n = snprintf(piece, sizeof(piece), one, numbers[next_number++]);
                    put(piece, n < 0 ? 0 : ((size_t)n < sizeof(piece) ? (size_t)n : sizeof(piece) - 1));
                } else {
                    put("?", 1);
                }
            } else if (type == 's') {
                if (next_text < text_count) {
                    memcpy(one, p, spec_length);
                    one[spec_length] = '\0';
                    const char* text = texts[next_text++];
                    const int n = snprintf(piece, sizeof(piece), one, text != nullptr ? text : "");
                    put(piece, n < 0 ? 0 : ((size_t)n < sizeof(piece) ? (size_t)n : sizeof(piece) - 1));
                } else {
                    put("?", 1);
                }
            } else {
                put(p, spec_length);
            }
            p = spec + 1;
        }
        out[at] = '\0';
    }
}

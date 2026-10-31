#include "pch.h"

#include "antares/rules_trace.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <dlfcn.h>
#endif

#include "antares/identity.h"
#include "antares/rng_trace.h"
#include "game/savegame.h"

namespace antares::rules_trace {
    namespace {
        int s_enabled = -1;
        uint64_t s_calls_at_last_mark = 0;

        bool Enabled() {
            if (s_enabled < 0) {
                const char* value = antares::Get_Env("RULES_TRACE");
                s_enabled = value != nullptr && value[0] != '\0' && strcmp(value, "0") != 0 ? 1 : 0;
                rng_trace::g_by_caller = s_enabled == 1;
            }
            return s_enabled == 1;
        }

        // FNV-1a over the save the game would write now, with the seed zeroed (it's logged on its
        // own) and a fixed description.
        uint64_t State_Hash(std::string* error) {
            uint8_t* buffer = static_cast<uint8_t*>(malloc(SAVEGAME::BUFFER_CAPACITY));
            if (buffer == nullptr) {
                *error = "out of memory";
                return 0;
            }
            SERIAL::Writer_ writer(buffer, SAVEGAME::BUFFER_CAPACITY, "rules trace buffer overflow");
            char description[sizeof(SAVEGAME::s_metadata::description)] = "rules-trace";
            uint64_t hash = 0;
            if (SAVEGAME::Write_Game(&writer, description, 0, error)) {
                hash = 0xCBF29CE484222325ull;
                for (uint32_t i = 0; i < writer.Length(); ++i) {
                    hash = (hash ^ buffer[i]) * 0x100000001B3ull;
                }
            }
            free(buffer);
            return hash;
        }

        // The caller's offset in its module, so addr2line can name it on a PIE build.
        uintptr_t Module_Offset(const void* address) {
#if defined(__linux__)
            Dl_info info{};
            if (dladdr(address, &info) != 0 && info.dli_fbase != nullptr) {
                return reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(info.dli_fbase);
            }
#endif
            return reinterpret_cast<uintptr_t>(address);
        }
    } // namespace

    void Mark(const char* where) {
        if (!Enabled()) {
            return;
        }
        std::string error;
        const uint64_t hash = State_Hash(&error);
        const uint64_t calls = rng_trace::g_calls - s_calls_at_last_mark;
        s_calls_at_last_mark = rng_trace::g_calls;
        SDL_Log("rules-trace: %s stardate=%d seed=%08x calls=%llu state=%016llx%s%s",
            where,
            (int)MOX::stardate,
            (unsigned)game_random::Get_Random_Seed(),
            (unsigned long long)calls,
            (unsigned long long)hash,
            error.empty() ? "" : " error=",
            error.c_str());

        std::vector<std::pair<const void*, uint64_t>> callers(rng_trace::g_callers.begin(), rng_trace::g_callers.end());
        std::sort(callers.begin(), callers.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& [caller, count] : callers) {
            SDL_Log("rules-trace:   caller 0x%llx %llu", (unsigned long long)Module_Offset(caller), (unsigned long long)count);
        }
        rng_trace::g_callers.clear();
    }
} // namespace antares::rules_trace

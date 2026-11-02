#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Records when each LBX file was last opened, so test scripts can wait for a screen
// (`expect_screen <lbx>`) instead of guessing a delay. Header-only so every target that links
// the VFS gets it without build changes. Thread-safe: LBX files load on the game thread and
// scripts run on the platform thread.
namespace antares::lbx_trace {
    inline std::mutex g_mutex;
    inline std::unordered_map<std::string, uint64_t> g_last_load;
    inline std::atomic<uint64_t> g_serial{0};

    inline std::string Key_(const char* lbx_name) {
        std::string key;
        for (const char* cursor = lbx_name; cursor != nullptr && *cursor != '\0'; ++cursor) {
            if (*cursor == '/' || *cursor == '\\') {
                key.clear();
            } else {
                key += static_cast<char>(std::toupper(static_cast<unsigned char>(*cursor)));
            }
        }
        if (key.size() < 4 || key.compare(key.size() - 4, 4, ".LBX") != 0) {
            key += ".LBX";
        }
        return key;
    }

    /** Called for every LBX entry opened. */
    inline void Note_Load(const char* lbx_name) {
        const uint64_t serial = ++g_serial;
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_last_load[Key_(lbx_name)] = serial;
    }

    /** The current serial; a later load of any LBX gets a higher one. */
    inline uint64_t Serial() {
        return g_serial.load();
    }

    /** The `count` most recently loaded LBX files, newest first (a hint to the current screen). */
    inline std::vector<std::string> Recent(size_t count) {
        std::vector<std::pair<uint64_t, std::string>> loads;
        {
            const std::lock_guard<std::mutex> lock(g_mutex);
            for (const auto& [name, serial] : g_last_load) {
                loads.emplace_back(serial, name);
            }
        }
        std::sort(loads.begin(), loads.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<std::string> names;
        for (size_t i = 0; i < loads.size() && i < count; ++i) {
            names.push_back(loads[i].second);
        }
        return names;
    }

    /** The serial of the last load of `lbx_name`, or 0 when it hasn't loaded. */
    inline uint64_t Last_Load(const char* lbx_name) {
        const std::lock_guard<std::mutex> lock(g_mutex);
        const auto found = g_last_load.find(Key_(lbx_name));
        return found == g_last_load.end() ? 0 : found->second;
    }
} // namespace antares::lbx_trace

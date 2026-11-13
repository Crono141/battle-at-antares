#include "pch.h"

#include <map>
#include <string>

// Stand-ins for what src/antares/save_queries.cpp calls (the native Load and Save), for
// tests/unit/save_queries_test.cpp: the slots are an in-memory table, not SAVE*.GAM files.

namespace TEST_SAVE {
    struct s_fake_slot {
        int32_t type;
        std::string description;
        int32_t stardate;
    };
    std::map<int, s_fake_slot> slots; // by slot number (SAVE<n+1>.GAM)
    int saves = 0;
    int loads = 0;
    int16_t last_slot = -1;
    bool load_ok = true;

    void Reset() {
        slots.clear();
        saves = loads = 0;
        last_slot = -1;
        load_ok = true;
    }

    void Put(int16_t slot, int32_t type, const char* description, int32_t stardate) {
        slots[slot] = {type, description, stardate};
    }

    int Slot_Of(const char* path) {
        int n = 0;
        return sscanf(path, "SAVE%d.GAM", &n) == 1 ? n - 1 : -1;
    }
} // namespace TEST_SAVE

namespace MOX {
    s_save_game_description save_game_description[10];
} // namespace MOX

bool File_Exists(const char* path) {
    return TEST_SAVE::slots.count(TEST_SAVE::Slot_Of(path)) != 0;
}

// The header as a save starts: type, description, stardate.
FILE* fopen_case(const char* path, const char* mode) {
    const auto it = TEST_SAVE::slots.find(TEST_SAVE::Slot_Of(path));
    if (it == TEST_SAVE::slots.end() || mode[0] != 'r') {
        return nullptr;
    }
    FILE* file = tmpfile();
    char description[sizeof(MOX::save_game_description[0].description)] = {};
    strlcpy(description, it->second.description.c_str(), sizeof(description));
    fwrite(&it->second.type, sizeof(int32_t), 1, file);
    fwrite(description, sizeof(description), 1, file);
    fwrite(&it->second.stardate, sizeof(int32_t), 1, file);
    rewind(file);
    return file;
}

namespace LOADSAVE {
    uint8_t loaded_new_game = 0;
    bool __cdecl Is_Valid_Game_Type(uint32_t type) {
        return type <= 3;
    }
    void __cdecl Save_Session_Related_Settings() {}
} // namespace LOADSAVE

namespace FILEDEF {
    void __cdecl Save_Game(int16_t slot) {
        ++TEST_SAVE::saves;
        TEST_SAVE::last_slot = slot;
    }
    uint32_t __cdecl Load_Game(int16_t slot) {
        ++TEST_SAVE::loads;
        TEST_SAVE::last_slot = slot;
        return TEST_SAVE::load_ok ? 1 : 0;
    }
} // namespace FILEDEF

namespace COLXPORT {
    int16_t __cdecl N_Colonies(int16_t) {
        return 3;
    }
} // namespace COLXPORT

namespace MISC {
    void __cdecl Get_Time_Stamp(const char*, int16_t* month, int16_t* day, int16_t* year, int16_t* hour, int16_t* minute, int16_t* second) {
        *month = 10;
        *day = 13;
        *year = 2026;
        *hour = 14;
        *minute = 5;
        *second = 0;
    }
} // namespace MISC

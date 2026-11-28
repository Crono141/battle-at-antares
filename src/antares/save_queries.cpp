#include "pch.h"

#include "antares/commands_internal.h"

#include <string>

// The game menu's Load and Save pages without the legacy popup (owner, 2026-11-13: they brought
// the legacy galaxy and its animated slots up over the native galaxy). The `saves` query reads the
// ten slots' headers as FILEDEF::Get_Saved_Game_Descriptions does, but without its globals;
// `save_game` and `load_game` do what LOADSAVE::Do_Save_Game_Popup and Do_Load_Game_Popup do once
// a slot is picked.

namespace antares::commands::internal {
    namespace {
        constexpr int16_t k_slots = 10;
        constexpr int16_t k_auto_save_slot = 9; // SAVE10.GAM, "(Auto Save)"

        void Slot_File(int16_t slot, char* name, size_t size) {
            snprintf(name, size, "SAVE%d.GAM", (int)slot + 1);
        }

        struct s_slot_header {
            bool exists = false;
            bool valid = false;
            int32_t type = -1;
            int32_t stardate = -1;
            std::string description;
        };

        // A save's header: game type, description, stardate (the start of SAVE*.GAM).
        s_slot_header Read_Header(int16_t slot) {
            s_slot_header header;
            char name[16];
            Slot_File(slot, name, sizeof(name));
            if (!File_Exists(name)) {
                return header;
            }
            header.exists = true;
            FILE* file = fopen_case(name, "rb");
            if (file == nullptr) {
                return header;
            }
            char description[sizeof(MOX::save_game_description[0].description)] = {};
            const bool read = fread(&header.type, sizeof(header.type), 1, file) == 1 &&
                fread(description, sizeof(description), 1, file) == 1 &&
                fread(&header.stardate, sizeof(header.stardate), 1, file) == 1;
            fclose(file);
            if (read && LOADSAVE::Is_Valid_Game_Type((uint32_t)header.type)) {
                description[sizeof(description) - 1] = '\0';
                header.valid = true;
                header.description = Utf8(description, sizeof(description));
            }
            return header;
        }

        // The file's date as the slots show it ("Nov 13, 2026  14:05"), English month names.
        std::string File_Date(int16_t slot) {
            static const char* const k_months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            char name[16];
            Slot_File(slot, name, sizeof(name));
            int16_t month = -1;
            int16_t day = 0;
            int16_t year = 0;
            int16_t hour = 0;
            int16_t minute = 0;
            int16_t second = 0;
            MISC::Get_Time_Stamp(name, &month, &day, &year, &hour, &minute, &second);
            if (month < 0 || month >= 12 || day < 1 || day > 31 || year <= 0) {
                return {};
            }
            if (year < 1900) {
                year += 1900; // years since 1900, as the time stamp gives them
            }
            char text[32];
            snprintf(text, sizeof(text), "%s %d, %d  %02d:%02d", k_months[month], (int)day, (int)year, (int)hour, (int)minute);
            return text;
        }

        bool Slot_Arg(const s_args& args, int16_t* slot, s_out& out) {
            int64_t value = -1;
            if (!args.Int("slot", &value) || value < 0 || value >= k_slots) {
                out.Fail("bad_args", "slot: 0-9 (SAVE1-SAVE10.GAM; 9 is the auto save)");
                return false;
            }
            *slot = (int16_t)value;
            return true;
        }

        void Query_Saves(int16_t, const s_args&, s_out& out) {
            out.Int("active", MOX::settings.active_save_slot);
            out.Bool("can_save", MOX::disallow_saves_code == 0);
            s_out list = out.Add_List("slots");
            for (int16_t slot = 0; slot < k_slots; ++slot) {
                const s_slot_header header = Read_Header(slot);
                s_out item = list.Append_Object();
                item.Int("slot", slot);
                // As Check_For_Saved_Games: empty, bad, or a network game (not loadable here).
                const bool network = header.valid && (header.type == 2 || header.type == 3);
                item.Str("state", !header.exists ? "empty" : (!header.valid ? "bad" : (network ? "network" : "ok")));
                if (header.valid) {
                    std::string description = header.description;
                    if (slot == k_auto_save_slot) {
                        description = "(Auto Save)";
                    } else if (description.empty()) {
                        description = "<< no description >>";
                    }
                    item.Str("description", description);
                    item.Int("stardate", header.stardate);
                    item.Str("date", File_Date(slot)); // the file exists: its time stamp
                }
            }
        }

        // Do_Save_Game_Popup's save: the slot becomes the active one; an empty description is the
        // game's "<ruler>, <race>, N colonies"; then the session settings, as the popup saves them.
        void Save_Game(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t slot = -1;
            if (!Slot_Arg(args, &slot, out)) {
                return;
            }
            if (MOX::disallow_saves_code != 0) {
                out.Fail("refused", Utf8(HAROLD::H_Message(0xB9), 200)); // "can't save now"
                return;
            }
            std::string description;
            args.Str("description", &description);
            char* target = MOX::save_game_description[slot].description;
            const size_t size = sizeof(MOX::save_game_description[slot].description);
            if (description.empty()) {
                const int16_t colonies = COLXPORT::N_Colonies(MOX::PLAYER_NUM);
                const s_player& player = MOX::player[MOX::PLAYER_NUM];
                snprintf(target, size, "%.19s, %.14s, %d %.11s", player.name, player.race_name, (int)colonies,
                    HAROLD::H_Message(colonies == 1 ? 0xBC : 0xBD));
            } else {
                // The popup's input field takes printable ASCII up to the description's size.
                std::string clean;
                for (const char c : description) {
                    if (c >= 0x20 && c < 0x7F && clean.size() + 1 < size) {
                        clean.push_back(c);
                    }
                }
                strlcpy(target, clean.c_str(), size);
            }
            MOX::settings.active_save_slot = (int8_t)slot;
            FILEDEF::Save_Game(slot);
            LOADSAVE::Save_Session_Related_Settings();
            out.Bool("saved", true);
            out.Str("description", Utf8(target, size));
        }

        // Do_Load_Game_Popup's load, then _Game_Popup's way out: the game goes to the next turn's
        // reports (or the hotseat player pick), which hands back to the galaxy.
        void Load_Game(int16_t, const s_args& args, s_out& out) {
            int16_t slot = -1;
            if (!Slot_Arg(args, &slot, out)) {
                return;
            }
            const s_slot_header header = Read_Header(slot);
            if (!header.valid || header.type == 2 || header.type == 3) {
                out.Fail("refused", !header.exists ? "That slot is empty." : "That save can't be loaded here.");
                return;
            }
            const int16_t from = MOX::current_screen;
            if (FILEDEF::Load_Game(slot) == 0) {
                char text[120];
                snprintf(text, sizeof(text), HAROLD::H_Message(184), (int)slot + 1); // "could not load"
                out.Fail("failed", Utf8(text, sizeof(text)));
                MOX::current_screen = from;
                return;
            }
            MOX::settings.active_save_slot = (int8_t)slot;
            LOADSAVE::loaded_new_game = 1;
            // Not Update_Game_Settings: it copies the popup's option fields, which only the
            // legacy popup fills.
            LOADSAVE::Save_Session_Related_Settings();
            MOX::return_screen = MOX::game_type == 1 ? SCREEN_HOTSEAT_SELECT_PLAYER : SCREEN_REPORTS;
            MOX::current_screen = MOX::return_screen;
            out.Bool("loaded", true);
        }
    } // namespace

    void Add_Save_Entries(std::vector<s_entry>* table) {
        table->push_back({"saves", e_kind::query, ANY_SCREEN, 1, "", Query_Saves});
        table->push_back({"save_game", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "slot: 0-9, description (optional)", Save_Game});
        table->push_back({"load_game", e_kind::command, Screen_Bit(SCREEN_MAIN) | Screen_Bit(SCREEN_MAIN_MENU), 0, "slot: 0-9", Load_Game});
    }

} // namespace antares::commands::internal

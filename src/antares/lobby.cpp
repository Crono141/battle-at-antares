#include "pch.h"

#include "antares/commands_internal.h"
#include "antares/identity.h"
#include "antares/lobby.h"
#include "config/mod_manager.h"
#include "game/newgame_options.h"
#include "game/string_overrides.h"
#include "yyjson.h"

#include <algorithm>
#include <string>

// WP3.13 (D30, release 0.9): the title screen, the new game and the Hall of Fame as native views.
// Each command does what the legacy screen does once the player has chosen (mainmenu.cpp,
// newgame.cpp, racesel.cpp, raceopt.cpp, score.cpp); the choosing itself is the frontend's.
// Single player only: hotseat and network new games keep the legacy screens (Native_Allowed).

namespace antares::lobby {
    namespace {
        int16_t s_flash = -1; // the Hall of Fame entry to flash (the end of a game), or -1
    }

    int16_t Flash_Entry() {
        return s_flash;
    }

    void Set_Flash_Entry(int16_t entry) {
        s_flash = entry;
    }

    void Enter(int16_t screen) {
        if (screen == SCREEN_NEW_GAME) {
            // As Newgame_Screen on entry (newgame.cpp): the game arrays are cleared before the race
            // picks write player 0, and the taken races and banners are reset (game types 0 and 1).
            struct_::Clear_Structure(MOX::star, MAX_STARS * sizeof(*MOX::star));
            struct_::Clear_Structure(MOX::planet, MAX_PLANETS * sizeof(*MOX::planet));
            struct_::Clear_Structure(MOX::colony, MAX_COLONIES * sizeof(*MOX::colony));
            struct_::Clear_Structure(MOX::ship, MAX_SHIPS * sizeof(*MOX::ship));
            struct_::Clear_Structure(MOX::player, MAX_PLAYERS * sizeof(*MOX::player));
            struct_::Clear_Structure(MOX::leaders, MAX_LEADERS * sizeof(*MOX::leaders));
            if (MOX::game_type == 0 || MOX::game_type == 1) {
                memset(RACESEL::color_flag, 0, sizeof(RACESEL::color_flag));
                memset(RACESEL::race_flag, 0, sizeof(RACESEL::race_flag));
            }
            NEWGAME::Globals_To_Temps();
        } else if (screen == SCREEN_HALL_OF_FAME) {
            // As Hall_Of_Fame_Screen on entry (score.cpp): a missing or old table is replaced by
            // the generated one.
            if (SCORE::HOF_Version_Number() != 0x82) {
                SCORE::Create_Default_HOF_File();
            }
        } else if (screen == SCREEN_MAIN_MENU) {
            MOX::game_type = 0; // as Main_Menu_Screen on every entry
        }
    }
} // namespace antares::lobby

namespace antares::commands::internal {
    namespace {
        constexpr char k_last_race_file[] = "ORION2RE.RAC"; // racesel.cpp, LAST_RACE_FILENAME

        bool Any_Numbered_Save() {
            // The menu's Load: "save?.gam", one character, so SAVE1-9 only (mainmenu.cpp).
            for (int slot = 1; slot <= 9; ++slot) {
                char name[16];
                snprintf(name, sizeof(name), "SAVE%d.GAM", slot);
                if (File_Exists(name)) {
                    return true;
                }
            }
            return false;
        }

        bool Can_Continue() {
            const int8_t type = FILEDEF::Get_Game_Type_From_Saved_Game(9);
            return (type == 0 || type == 1) && File_Exists("SAVE10.GAM");
        }

        // ---- The title screen (mainmenu.cpp, Main_Menu_Screen) ----

        void Query_Menu(int16_t, const s_args&, s_out& out) {
            out.Bool("can_continue", Can_Continue());
            out.Bool("can_load", MOX::disallow_saves_code == 0 && Any_Numbered_Save());
            out.Str("version_label", antares::PRODUCT_VERSION_LABEL); // "version" is the query's own
            out.Bool("show_mod_list", MAINMENU::show_mod_list);
            s_out mods = out.Add_List("mods");
            for (const config::s_loaded_mod& mod : config::Get_Loaded_Mods()) {
                s_out item = mods.Append_Object();
                item.Str("id", mod.manifest.id);
                item.Str("name", mod.manifest.name);
                item.Str("version", mod.manifest.version_text);
                item.Int("priority", (int64_t)mod.priority_index);
            }
        }

        // The menu's buttons once chosen: "continue" loads SAVE10 (the game's own routing),
        // "new", "hall_of_fame", "multiplayer", "credits" and "quit" set the next screen.
        void Menu_Choose(int16_t, const s_args& args, s_out& out) {
            std::string item;
            if (!args.Str("item", &item)) {
                out.Fail("bad_args", "item: continue | new | hall_of_fame | multiplayer | credits | quit");
                return;
            }
            if (item == "continue") {
                if (!Can_Continue()) {
                    out.Fail("refused", "There is no game to continue.");
                    return;
                }
                if (FILEDEF::Load_Game(9) == 0) {
                    MOX::current_screen = SCREEN_MAIN_MENU;
                    out.Fail("failed", "The last game could not be loaded.");
                    return;
                }
                if (MOX::game_type == 2 || MOX::game_type == 3) {
                    MOX::current_screen = SCREEN_MAIN_MENU;
                    out.Fail("refused", Utf8(HAROLD::H_Message(207), 300));
                    return;
                }
                MOX::current_screen = MOX::game_type == 1 ? SCREEN_HOTSEAT_SELECT_PLAYER : SCREEN_REPORTS;
            } else if (item == "new") {
                MOX::game_type = 0;
                MOX::current_screen = SCREEN_NEW_GAME;
                MOX::return_screen = SCREEN_MAIN;
            } else if (item == "hall_of_fame") {
                lobby::Set_Flash_Entry(-1);
                MOX::current_screen = SCREEN_HALL_OF_FAME;
                MOX::return_screen = SCREEN_MAIN_MENU;
            } else if (item == "multiplayer") {
                MOX::current_screen = SCREEN_MULTI_PLAYER;
                MOX::return_screen = SCREEN_MAIN_MENU;
            } else if (item == "credits") {
                MOX::current_screen = SCREEN_CREDITS;
                MOX::return_screen = SCREEN_MAIN_MENU;
            } else if (item == "quit") {
                MOX::current_screen = SCREEN_EXIT;
            } else {
                out.Fail("bad_args", "item: continue | new | hall_of_fame | multiplayer | credits | quit");
                return;
            }
            MAINSCR2::main_screen_fade_in_flag = 1;
            MOX::FULL_DRAW_SCREEN_FLAG = 0;
            out.Bool("done", true);
        }

        // ---- Hall of Fame (score.cpp) ----

        const char* Difficulty_Name(int16_t difficulty) {
            static const int16_t ids[GAME_DIFFICULTY_COUNT] = {0x221, 0x118, 0xBE, 0x13E, 0x148};
            if (difficulty < 0 || difficulty >= GAME_DIFFICULTY_COUNT) {
                return "";
            }
            return ESTRINGS::E_Strings(ids[difficulty]);
        }

        void Query_Hall_Of_Fame(int16_t, const s_args&, s_out& out) {
            const bool read = SCORE::Read_HOF_File() != 0;
            out.Int("flash", lobby::Flash_Entry());
            s_out rows = out.Add_List("rows");
            if (!read) {
                return;
            }
            int16_t order[10];
            for (int16_t i = 0; i < 10; ++i) {
                order[i] = i;
            }
            std::stable_sort(order, order + 10, [](int16_t a, int16_t b) {
                return SCORE::hof.scores[a] > SCORE::hof.scores[b];
            });
            for (int16_t rank = 0; rank < 10; ++rank) {
                const int16_t i = order[rank];
                s_out row = rows.Append_Object();
                row.Int("record", i);
                row.Int("rank", rank + 1);
                row.Str("name", Utf8(SCORE::hof.player_names[i], sizeof(SCORE::hof.player_names[i])));
                row.Str("race", Utf8(SCORE::hof.race_names[i], sizeof(SCORE::hof.race_names[i])));
                row.Int("race_id", SCORE::hof.race_ids[i]);
                row.Int("difficulty", SCORE::hof.difficulties[i]);
                row.Str("difficulty_name", Utf8(Difficulty_Name(SCORE::hof.difficulties[i]), 40));
                row.Int("score", SCORE::hof.scores[i]);
            }
        }

        void Close_Hall_Of_Fame(int16_t, const s_args&, s_out& out) {
            lobby::Set_Flash_Entry(-1);
            MOX::current_screen = SCREEN_MAIN_MENU;
            out.Bool("closed", true);
        }

        // Alt+C in the legacy screen; the native view asks first (the legacy one doesn't).
        void Reset_Hall_Of_Fame(int16_t, const s_args&, s_out& out) {
            SCORE::Create_Default_HOF_File();
            out.Bool("reset", true);
        }

        // ---- New game (newgame.cpp, racesel.cpp, raceopt.cpp) ----

        struct s_option_texts {
            bool loaded = false;
            std::string text[64]; // racestuf.lbx's option table, as raceopt::opt_string
        };

        // The custom race screen's labels: group titles and choices (raceopt::Reload_Option_Strings
        // into a pushed screen block, as Racial_Option_Screen does around its own use).
        const s_option_texts& Option_Texts() {
            static s_option_texts texts;
            if (!texts.loaded) {
                allocate::Push_Block(MOX::screen_seg);
                raceopt::Reload_Option_Strings();
                for (int i = 0; i < 64; ++i) {
                    texts.text[i] = raceopt::opt_string[i] != nullptr ? Utf8(raceopt::opt_string[i], 64) : std::string();
                }
                allocate::Pop_Block(MOX::screen_seg);
                texts.loaded = true;
            }
            return texts;
        }

        // A race's traits as the selection screen lists them (race_display.cpp): primary flags
        // (1-3) are shown with their CFG values.
        void Race_Trait_Lines(const uint8_t* traits, s_out list) {
            for (int16_t ability = TRAIT_POP_GROWTH_BONUS; ability < TRAIT_LOW_G_WORLD; ++ability) {
                const int8_t level = (int8_t)traits[ability];
                if (level <= 0 || level > raceopt::PRIMARY_CHOICE_COUNT) {
                    continue;
                }
                const int8_t bonus = raceopt::pick_rules.primary[ability - TRAIT_POP_GROWTH_BONUS][level - 1].value;
                char value[16];
                raceopt::Format_Numeric_Trait(value, sizeof(value), ability, bonus);
                std::string line = Utf8(MOX::special_ability_names[ability], 40);
                line += ability == TRAIT_PRODUCTION_BONUS ? ": " : " ";
                line += Utf8(value, sizeof(value));
                list.Append_Object().Str("text", line);
            }
            for (int16_t ability = TRAIT_LOW_G_WORLD; ability < TRAIT_COUNT; ++ability) {
                if (traits[ability] == 0) {
                    continue;
                }
                const int name_id = ability == TRAIT_RICH_HOME_WORLD && (int8_t)traits[ability] < 0 ? TRAIT_COUNT : ability;
                list.Append_Object().Str("text", Utf8(MOX::special_ability_names[name_id], 40));
            }
            const int8_t government = (int8_t)traits[TRAIT_CURRENT_GOVERNMENT];
            if (government >= 0 && government < GOVERNMENT_COUNT) {
                list.Append_Object().Str("text", Utf8(MOX::government_labels[government], 40));
            }
        }

        // The custom race screen's picks for a set of traits whose primary entries are flags
        // (raceopt::Convert_Player_To_Flags on a scratch player; the screen's globals are kept).
        void Picks_Of(const uint8_t* traits, s_out out) {
            int16_t saved_buttons[10];
            int16_t saved_toggles[24];
            memcpy(saved_buttons, raceopt::button_variable, sizeof(saved_buttons));
            memcpy(saved_toggles, raceopt::option_toggle, sizeof(saved_toggles));
            s_player scratch{};
            memcpy(scratch.traits, traits, sizeof(scratch.traits));
            raceopt::Convert_Player_To_Flags(&scratch);
            s_out primary = out.Add_List("primary");
            for (int i = 0; i < raceopt::PRIMARY_PICK_COUNT; ++i) {
                primary.Append_Int(raceopt::button_variable[i]);
            }
            out.Int("government", raceopt::button_variable[9]);
            s_out specials = out.Add_List("specials");
            for (int i = 0; i < raceopt::SPECIAL_PICK_COUNT; ++i) {
                specials.Append_Int(raceopt::option_toggle[i] != 0 ? 1 : 0);
            }
            memcpy(raceopt::button_variable, saved_buttons, sizeof(saved_buttons));
            memcpy(raceopt::option_toggle, saved_toggles, sizeof(saved_toggles));
        }

        struct s_last_race {
            bool valid = false;
            uint8_t traits[TRAIT_COUNT] = {};
            uint8_t race = 0;
            char name[15] = {};
        };

        // ORION2RE.RAC: the last custom race (traits as values, the base race, the race name).
        s_last_race Read_Last_Race() {
            s_last_race last;
            FILE* file = fopen_case(k_last_race_file, "rb");
            if (file == nullptr) {
                return last;
            }
            last.valid = fread(last.traits, sizeof(last.traits), 1, file) == 1 && fread(&last.race, sizeof(last.race), 1, file) == 1 &&
                fread(last.name, sizeof(last.name), 1, file) == 1 && last.race < STOCK_RACE_COUNT;
            last.name[sizeof(last.name) - 1] = '\0';
            fclose(file);
            if (last.valid) {
                RACESEL::Convert_Custom_Race_Flags(last.traits); // values back to flags
            }
            return last;
        }

        // RACENAME.LBX: eight leader names per race (racesel.cpp, Naming_Popup), JSON5 overridable.
        void Leader_Names(int16_t race, s_out list) {
            for (int16_t k = 0; k < 8; ++k) {
                const int16_t index = (int16_t)(race * 8 + k);
                char name[20] = {};
                if (!Lbx_Record_Bytes("RACENAME.LBX", 0, (uint16_t)index, name, sizeof(name))) {
                    continue;
                }
                name[sizeof(name) - 1] = '\0';
                string_overrides::Apply_String("RACENAME.LBX", (size_t)index + 1, name, sizeof(name));
                list.Append_Object().Str("name", Utf8(name, sizeof(name)));
            }
        }

        void Query_New_Game(int16_t, const s_args&, s_out& out) {
            NEWGAME::Globals_To_Temps();
            s_out settings = out.Add_Object("settings");
            settings.Int("difficulty", NEWGAME::diff_button_variable);
            settings.Int("size", NEWGAME::gal_size_button_variable);
            settings.Int("age", NEWGAME::gal_age_button_variable);
            settings.Int("players", NEWGAME::opp_button_variable);
            settings.Int("tech", NEWGAME::civ_button_variable);
            settings.Bool("tactical", NEWGAME::combat_button_variable != 0);
            settings.Bool("events", NEWGAME::random_events_button_variable != 0);
            settings.Bool("antarans", NEWGAME::xenon_attacks_button_variable != 0);
            out.Bool("has_maximum", MAX_STARS > ORIGINAL_MAX_STARS);

            s_out difficulties = out.Add_List("difficulties");
            for (int16_t d = 0; d < GAME_DIFFICULTY_COUNT; ++d) {
                difficulties.Append_Object().Str("name", Utf8(Difficulty_Name(d), 40));
            }

            s_out races = out.Add_List("races");
            for (int16_t race = 0; race < STOCK_RACE_COUNT; ++race) {
                s_out item = races.Append_Object();
                item.Int("race", race);
                item.Str("name", Utf8(MOX::race_names[race], 20));
                item.Bool("taken", RACESEL::race_flag[race] != 0);
                Race_Trait_Lines(RACESEL::race_specials[race], item.Add_List("traits"));
                Picks_Of(RACESEL::race_specials[race], item.Add_Object("picks"));
                Leader_Names(race, item.Add_List("leaders"));
            }

            const s_last_race last = Read_Last_Race();
            if (last.valid) {
                s_out item = out.Add_Object("last_custom");
                item.Int("race", last.race);
                item.Str("name", Utf8(last.name, sizeof(last.name)));
                Race_Trait_Lines(last.traits, item.Add_List("traits"));
                Picks_Of(last.traits, item.Add_Object("picks"));
            }

            const s_option_texts& texts = Option_Texts();
            const raceopt::s_pick_rules& rules = raceopt::pick_rules;
            s_out picks = out.Add_Object("pick_rules");
            picks.Int("starting", rules.starting);
            picks.Int("negative_limit", rules.negative_limit);
            s_out groups = picks.Add_List("primary");
            for (int g = 0; g < raceopt::PRIMARY_PICK_COUNT; ++g) {
                s_out group = groups.Append_Object();
                group.Str("title", texts.text[g * 4]);
                s_out choices = group.Add_List("choices");
                for (int c = 0; c < raceopt::PRIMARY_CHOICE_COUNT; ++c) {
                    s_out choice = choices.Append_Object();
                    choice.Str("label", texts.text[g * 4 + c + 1]);
                    choice.Int("cost", rules.primary[g][c].cost);
                }
            }
            s_out government = picks.Add_Object("government");
            government.Str("title", texts.text[36]);
            s_out governments = government.Add_List("choices");
            for (int g = 0; g < raceopt::GOVERNMENT_PICK_COUNT; ++g) {
                s_out choice = governments.Append_Object();
                choice.Str("label", texts.text[37 + g]);
                choice.Int("cost", rules.government[g]);
            }
            picks.Str("specials_title", texts.text[41]);
            s_out specials = picks.Add_List("specials");
            for (int s = 0; s < raceopt::SPECIAL_PICK_COUNT; ++s) {
                s_out special = specials.Append_Object();
                special.Str("label", texts.text[42 + s]);
                special.Int("cost", rules.special[s]);
            }
            // Pairs that switch each other off (racesel.cpp, Racial_Option_Screen).
            s_out exclusive = picks.Add_List("exclusive");
            const int pairs[][2] = {
                {raceopt::SPECIAL_PICK_RICH_HOME_WORLD, raceopt::SPECIAL_PICK_POOR_HOME_WORLD},
                {raceopt::SPECIAL_PICK_LOW_GRAVITY, raceopt::SPECIAL_PICK_HIGH_GRAVITY},
                {raceopt::SPECIAL_PICK_LITHOVORE, raceopt::SPECIAL_PICK_CYBERNETIC},
                {raceopt::SPECIAL_PICK_REPULSIVE, raceopt::SPECIAL_PICK_CHARISMATIC},
                {raceopt::SPECIAL_PICK_UNCREATIVE, raceopt::SPECIAL_PICK_CREATIVE},
            };
            for (const auto& pair : pairs) {
                s_out p = exclusive.Append_List();
                p.Append_Int(pair[0]);
                p.Append_Int(pair[1]);
            }
            picks.Int("lithovore", raceopt::SPECIAL_PICK_LITHOVORE);
            picks.Int("food_group", raceopt::PRIMARY_PICK_FOOD);
        }

        // A whole number (JSON from some frontends sends 2.0 for 2).
        bool Whole(yyjson_val* val, int64_t* value) {
            if (yyjson_is_int(val)) {
                *value = yyjson_get_sint(val);
                return true;
            }
            if (yyjson_is_real(val)) {
                const double d = yyjson_get_real(val);
                if (d == (double)(int64_t)d) {
                    *value = (int64_t)d;
                    return true;
                }
            }
            return false;
        }

        bool Range_Arg(const s_args& args, const char* key, int64_t low, int64_t high, int64_t* value, s_out& out, bool optional = true) {
            if (optional && args.Get(key) == nullptr) {
                return true; // left out: as it was
            }
            if (!Whole(args.Get(key), value) || *value < low || *value > high) {
                out.Fail("bad_args", std::string(key) + ": " + std::to_string(low) + "-" + std::to_string(high));
                return false;
            }
            return true;
        }

        bool Bool_Arg(const s_args& args, const char* key, bool* value, s_out& out) {
            yyjson_val* val = args.Get(key);
            if (val == nullptr) {
                return true; // left out: as it was
            }
            if (!yyjson_is_bool(val)) {
                out.Fail("bad_args", std::string(key) + ": true or false");
                return false;
            }
            *value = yyjson_get_bool(val);
            return true;
        }

        // Printable ASCII up to the field's size (the game's input boxes take nothing else).
        void Copy_Name(const std::string& text, char* target, size_t size) {
            std::string clean;
            for (const char c : text) {
                if (c >= 0x20 && c < 0x7F && clean.size() + 1 < size) {
                    clean.push_back(c);
                }
            }
            strlcpy(target, clean.c_str(), size);
        }

        // The custom picks, checked as the custom race screen checks each click and Accept
        // (racesel.cpp, Racial_Option_Screen). Leaves them in raceopt's globals.
        bool Apply_Custom_Picks(yyjson_val* custom, s_out& out) {
            yyjson_val* primary = yyjson_obj_get(custom, "primary");
            yyjson_val* specials = yyjson_obj_get(custom, "specials");
            yyjson_val* government = yyjson_obj_get(custom, "government");
            if (!yyjson_is_arr(primary) || yyjson_arr_size(primary) != raceopt::PRIMARY_PICK_COUNT || !yyjson_is_arr(specials) ||
                yyjson_arr_size(specials) != raceopt::SPECIAL_PICK_COUNT || !yyjson_is_num(government)) {
                out.Fail("bad_args", "custom: {primary: [9 x 0-3], government: 0-3, specials: [22 x 0|1], name}");
                return false;
            }
            for (int i = 0; i < raceopt::PRIMARY_PICK_COUNT; ++i) {
                int64_t v = -1;
                if (!Whole(yyjson_arr_get(primary, i), &v) || v < 0 || v > raceopt::PRIMARY_CHOICE_COUNT) {
                    out.Fail("bad_args", "custom.primary: 0 none, 1-3 a choice");
                    return false;
                }
                raceopt::button_variable[i] = (int16_t)v;
            }
            int64_t gov = -1;
            if (!Whole(government, &gov) || gov < 0 || gov >= raceopt::GOVERNMENT_PICK_COUNT) {
                out.Fail("bad_args", "custom.government: 0-3");
                return false;
            }
            raceopt::button_variable[9] = (int16_t)gov;
            for (int i = 0; i < raceopt::SPECIAL_PICK_COUNT; ++i) {
                int64_t on = 0;
                Whole(yyjson_arr_get(specials, i), &on);
                raceopt::option_toggle[i] = on != 0 ? 1 : 0;
            }
            const int pairs[][2] = {
                {raceopt::SPECIAL_PICK_RICH_HOME_WORLD, raceopt::SPECIAL_PICK_POOR_HOME_WORLD},
                {raceopt::SPECIAL_PICK_LOW_GRAVITY, raceopt::SPECIAL_PICK_HIGH_GRAVITY},
                {raceopt::SPECIAL_PICK_LITHOVORE, raceopt::SPECIAL_PICK_CYBERNETIC},
                {raceopt::SPECIAL_PICK_REPULSIVE, raceopt::SPECIAL_PICK_CHARISMATIC},
                {raceopt::SPECIAL_PICK_UNCREATIVE, raceopt::SPECIAL_PICK_CREATIVE},
            };
            for (const auto& pair : pairs) {
                if (raceopt::option_toggle[pair[0]] != 0 && raceopt::option_toggle[pair[1]] != 0) {
                    out.Fail("refused", "Two of the picks rule each other out.");
                    return false;
                }
            }
            if (raceopt::option_toggle[raceopt::SPECIAL_PICK_LITHOVORE] != 0 && raceopt::button_variable[raceopt::PRIMARY_PICK_FOOD] != 0) {
                char text[120];
                KEN::Ken_Get_Text_Message(0x49, text, sizeof(text));
                out.Fail("refused", Utf8(text, sizeof(text)));
                return false;
            }
            raceopt::Calculate_Picks();
            if (raceopt::negative_picks < raceopt::pick_rules.negative_limit) {
                char text[120];
                KEN::Ken_Get_Text_Message(0x4A, text, sizeof(text));
                out.Fail("refused", Utf8(text, sizeof(text)));
                return false;
            }
            if (raceopt::picks < 0) {
                char text[120];
                KEN::Ken_Get_Text_Message(0x4B, text, sizeof(text));
                out.Fail("refused", Utf8(text, sizeof(text)));
                return false;
            }
            return true;
        }

        // The setup screen's Accept, the race pick, naming and banner, then the dispatcher's
        // Init_New_Game (mox2.cpp, SCREEN_NEW_GAME): in one command, with the choices as arguments.
        void Create_Game(int16_t, const s_args& args, s_out& out) {
            if (MOX::game_type != 0) {
                out.Fail("refused", "Only single player games start here.");
                return;
            }
            // Options left out keep the saved settings (the setup screen opens on them).
            NEWGAME::Globals_To_Temps();
            int64_t difficulty = NEWGAME::diff_button_variable, size = NEWGAME::gal_size_button_variable,
                    age = NEWGAME::gal_age_button_variable, players = NEWGAME::opp_button_variable,
                    tech = NEWGAME::civ_button_variable, race = 0, color = 0;
            bool tactical = NEWGAME::combat_button_variable != 0, events = NEWGAME::random_events_button_variable != 0,
                 antarans = NEWGAME::xenon_attacks_button_variable != 0;
            const int64_t max_size = MAX_STARS > ORIGINAL_MAX_STARS ? NEWGAME::NEW_GAME_GALAXY_SIZE_MAXIMUM : NEWGAME::NEW_GAME_GALAXY_SIZE_HUGE;
            if (!Range_Arg(args, "difficulty", 0, GAME_DIFFICULTY_COUNT - 1, &difficulty, out) || !Range_Arg(args, "size", 0, max_size, &size, out) ||
                !Range_Arg(args, "age", 0, GALAXY_AGE_COUNT - 1, &age, out) ||
                !Range_Arg(args, "players", 0, NEWGAME::NEW_GAME_PLAYER_COUNT_RANDOM, &players, out) ||
                !Range_Arg(args, "tech", 0, STARTING_CIVILIZATION_LEVEL_COUNT - 1, &tech, out) ||
                !Bool_Arg(args, "tactical", &tactical, out) || !Bool_Arg(args, "events", &events, out) ||
                !Bool_Arg(args, "antarans", &antarans, out) || !Range_Arg(args, "race", 0, STOCK_RACE_COUNT - 1, &race, out, false) ||
                !Range_Arg(args, "color", 0, MAX_PLAYERS - 1, &color, out, false)) {
                return;
            }
            std::string leader;
            args.Str("leader", &leader);
            yyjson_val* custom = args.Get("custom");
            const bool is_custom = yyjson_is_obj(custom);
            if (is_custom && !Apply_Custom_Picks(custom, out)) {
                return;
            }

            // Setup screen Accept (newgame.cpp): the temps, then the settings.
            NEWGAME::diff_button_variable = (int16_t)difficulty;
            NEWGAME::gal_size_button_variable = (int16_t)size;
            NEWGAME::gal_age_button_variable = (int16_t)age;
            NEWGAME::opp_button_variable = (int16_t)players;
            NEWGAME::civ_button_variable = (int16_t)tech;
            NEWGAME::combat_button_variable = tactical ? 1 : 0;
            NEWGAME::random_events_button_variable = events ? 1 : 0;
            NEWGAME::xenon_attacks_button_variable = antarans ? 1 : 0;
            NEWGAME::Temps_To_Globals();

            // Race pick (racesel.cpp, Race_Selection_Screen): player 0 gets the race's traits.
            s_player* player = MOX::player;
            struct_::Copy_Structure(player->traits, RACESEL::race_specials[race], sizeof(player->traits));
            player->race = (uint8_t)race;
            strlcpy(player->race_name, MOX::race_names[race], sizeof(player->race_name));
            if (leader.empty()) {
                out.Fail("bad_args", "leader: the leader's name");
                return;
            }
            Copy_Name(leader, player->name, sizeof(player->name));
            player->color = (uint8_t)color;
            if (is_custom) {
                // Custom Accept: the picks become traits, and the race is remembered (ORION2RE.RAC).
                raceopt::Convert_Flags_To_Player(player);
                std::string race_name;
                yyjson_val* name = yyjson_obj_get(custom, "name");
                if (yyjson_is_str(name)) {
                    race_name = yyjson_get_str(name);
                }
                if (!race_name.empty()) {
                    Copy_Name(race_name, player->race_name, sizeof(player->race_name));
                }
                FILE* file = fopen_case(k_last_race_file, "wb");
                if (file != nullptr) {
                    fwrite(player->traits, sizeof(player->traits), 1, file);
                    fwrite(&player->race, sizeof(player->race), 1, file);
                    fwrite(player->race_name, sizeof(player->race_name), 1, file);
                    fclose(file);
                }
            } else {
                // A stock race's primary traits are flags 1-3: the CFG values replace them.
                for (int trait = TRAIT_POP_GROWTH_BONUS; trait < TRAIT_LOW_G_WORLD; ++trait) {
                    if (player->traits[trait] != 0) {
                        player->traits[trait] = raceopt::pick_rules.primary[trait - 1][player->traits[trait] - 1].value;
                    }
                }
            }
            RACESEL::race_flag[race] = 1;
            player->objectives = 100; // human (newgame.cpp)

            // Newgame_Screen's end, then the dispatcher.
            NEWGAME::Temps_To_Globals();
            if (NEWGAME::Random_Player_Count_Selected()) {
                MOX::settings.number_of_players = MAX_PLAYERS;
            }
            if (!INITGAME::Init_New_Game(MOX::settings.game_difficulty, MOX::settings.galaxy_size, MOX::settings.galaxy_age,
                    NEWGAME::Player_Count_For_Initialization(), MOX::settings.strategic_combat_flag,
                    MOX::settings.starting_civilization_level)) {
                MOX::current_screen = SCREEN_MAIN_MENU;
                out.Fail("failed", "The galaxy could not be created.");
                return;
            }
            // Init_Map_Defaults set the next screen (the first turn's reports).
            out.Bool("created", true);
        }

        void Cancel_New_Game(int16_t, const s_args&, s_out& out) {
            MOX::current_screen = SCREEN_MAIN_MENU;
            out.Bool("cancelled", true);
        }
    } // namespace

    void Add_Lobby_Entries(std::vector<s_entry>* table) {
        table->push_back({"menu", e_kind::query, ANY_SCREEN, 1, "", Query_Menu});
        table->push_back({"menu_choose", e_kind::command, Screen_Bit(SCREEN_MAIN_MENU), 0,
            "item: continue | new | hall_of_fame | multiplayer | credits | quit", Menu_Choose});
        table->push_back({"hall_of_fame", e_kind::query, ANY_SCREEN, 1, "", Query_Hall_Of_Fame});
        table->push_back({"close_hall_of_fame", e_kind::command, Screen_Bit(SCREEN_HALL_OF_FAME), 0, "", Close_Hall_Of_Fame});
        table->push_back({"reset_hall_of_fame", e_kind::command, Screen_Bit(SCREEN_HALL_OF_FAME), 0, "", Reset_Hall_Of_Fame});
        table->push_back({"new_game", e_kind::query, ANY_SCREEN, 1, "", Query_New_Game});
        table->push_back({"create_game", e_kind::command, Screen_Bit(SCREEN_NEW_GAME), 0,
            "difficulty, size, age, players (button 0-7, 7 random), tech (button), tactical, events, antarans, race 0-12, "
            "color 0-7, leader, custom {primary [9], government, specials [22], name} (optional)",
            Create_Game});
        table->push_back({"cancel_new_game", e_kind::command, Screen_Bit(SCREEN_NEW_GAME), 0, "", Cancel_New_Game});
    }

} // namespace antares::commands::internal

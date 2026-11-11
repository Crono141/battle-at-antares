#include "pch.h"

#include "antares/commands.h"
#include "antares/commands_internal.h"

#include "antares/identity.h"
#include "antares/rng_trace.h"
#include "antares/rules_trace.h"
#include "config/text_encoding.h"
#include "yyjson.h"

#include <algorithm>

namespace antares::commands {

    // ---- Arguments and results ----

    bool s_args::Has(const char* key) const {
        return yyjson_obj_get(object_, key) != nullptr;
    }

    bool s_args::Int(const char* key, int64_t* value) const {
        yyjson_val* val = yyjson_obj_get(object_, key);
        if (!yyjson_is_int(val)) {
            return false;
        }
        *value = yyjson_get_sint(val);
        return true;
    }

    bool s_args::Str(const char* key, std::string* value) const {
        const char* text = yyjson_get_str(yyjson_obj_get(object_, key));
        if (text == nullptr) {
            return false;
        }
        *value = text;
        return true;
    }

    bool s_args::Int_List(const char* key, std::vector<int64_t>* values) const {
        yyjson_val* list = yyjson_obj_get(object_, key);
        if (!yyjson_is_arr(list)) {
            return false;
        }
        values->clear();
        size_t index = 0;
        size_t count = 0;
        yyjson_val* item = nullptr;
        yyjson_arr_foreach(list, index, count, item) {
            if (!yyjson_is_int(item)) {
                return false;
            }
            values->push_back(yyjson_get_sint(item));
        }
        return true;
    }

    void s_out::Int(const char* key, int64_t value) {
        yyjson_mut_obj_put(object_, yyjson_mut_strcpy(doc_, key), yyjson_mut_sint(doc_, value));
    }

    void s_out::Bool(const char* key, bool value) {
        yyjson_mut_obj_put(object_, yyjson_mut_strcpy(doc_, key), yyjson_mut_bool(doc_, value));
    }

    void s_out::Str(const char* key, const std::string& value) {
        yyjson_mut_obj_put(object_, yyjson_mut_strcpy(doc_, key), yyjson_mut_strncpy(doc_, value.data(), value.size()));
    }

    s_out s_out::Add_Object(const char* key) {
        yyjson_mut_val* child = yyjson_mut_obj(doc_);
        yyjson_mut_obj_put(object_, yyjson_mut_strcpy(doc_, key), child);
        return s_out(doc_, child);
    }

    s_out s_out::Add_List(const char* key) {
        yyjson_mut_val* child = yyjson_mut_arr(doc_);
        yyjson_mut_obj_put(object_, yyjson_mut_strcpy(doc_, key), child);
        return s_out(doc_, child);
    }

    s_out s_out::Append_Object() {
        yyjson_mut_val* child = yyjson_mut_obj(doc_);
        yyjson_mut_arr_append(object_, child);
        return s_out(doc_, child);
    }

    void s_out::Append_Int(int64_t value) {
        yyjson_mut_arr_append(object_, yyjson_mut_sint(doc_, value));
    }

    void s_out::Fail(const char* code, const std::string& message) {
        failed_ = true;
        Str("error", code);
        Str("message", message);
    }

    namespace internal {

        std::string Utf8(const char* text, size_t max_length) {
            std::string decoded;
            if (text == nullptr) {
                return decoded;
            }
            config::Decode_Cp850_To_Utf8(text, strnlen(text, max_length), &decoded);
            std::string clean;
            clean.reserve(decoded.size());
            for (const char c : decoded) {
                if ((unsigned char)c >= 0x20 || c == '\n') {
                    clean.push_back(c);
                }
            }
            return clean;
        }

        bool Have_Game(s_out& out) {
            if (MOX::player == nullptr || MOX::PLAYER_NUM < 0 || MOX::PLAYER_NUM >= MOX::NUM_PLAYERS) {
                out.Fail("no_game", "no game is loaded");
                return false;
            }
            return true;
        }
    } // namespace internal

    namespace {
        using internal::Have_Game;
        using internal::Utf8;

        bool Ship_Alive(int16_t ship) {
            return MOX::ship[ship].status < 5;
        }

        // ---- Commands ----

        // What the legacy galaxy does when the turn button is pressed (Main_Screen, next_button).
        void End_Turn(int16_t, const s_args&, s_out&) {
            RUSS::Assert_Settings();
            MOX::auto_turn_exit = 0;
            MAINSCR::next_turn_hit = 1;
            MOX::global_exit_field = -1000;
            int16_t exit_flag = 0;
            MAINSCR::Do_Next_Turn(&exit_flag, 0);
        }

        // The screens the native galaxy may open through the legacy fallback, with the setup the
        // legacy galaxy does before it leaves for them (mainscr_main.cpp, Main_Screen).
        void Open(int16_t from, const s_args& args, s_out& out) {
            int64_t screen = SCREEN_UNKNOWN;
            if (!args.Int("screen", &screen)) {
                out.Fail("bad_args", "screen: a SCREEN_* number");
                return;
            }
            switch (screen) {
                case SCREEN_COLONY_SUMMARY:
                case SCREEN_INFO:
                case SCREEN_RACE:
                case SCREEN_PLANET_SUMMARY:
                case SCREEN_SHOW_COMMAND_POINTS:
                case SCREEN_DISTANCE_BETWEEN_STARS: // F9
                    break;
                case SCREEN_FLEET:
                    MAINSCR::officer_screen_return_screen = SCREEN_FLEET;
                    break;
                case SCREEN_GAME:
                    MOX::screen_data = 0;
                    break;
                case SCREEN_TECH_CHANGE:
                    // Tech_Change draws over the page it finds: put the galaxy there, as the legacy
                    // galaxy's research field does (Main_Screen, research_window_field).
                    MOX::dont_draw_main_screen_to_back = 1;
                    fields::Clear_Fields();
                    MAINSCR::Clear_Main_Screen_Fields();
                    video::Set_Page_Off();
                    MAINSCR::Draw_Mini_Main_Screen();
                    video::Copy_Off_To_Back();
                    RUSS::Mox_Sync_Update();
                    video::Set_Refresh_Stencil();
                    MAINSCR::clear_fleet_fields = 0;
                    break;
                case SCREEN_OFFICERS:
                    MOX::temp_star_handle = MOX::officer_scrn_type == LEADER_TYPE_SHIP
                        ? MAINSCR::Get_Ship_Stack_For_Officers_Screen()
                        : MAINSCR::Get_Star_Id_For_Officers_Screen();
                    MAINSCR::officer_screen_return_screen = (int8_t)from;
                    break;
                default:
                    // A colony or ship design needs setup the bridge doesn't do yet: refuse rather
                    // than open it half set up.
                    out.Fail("refused", "this screen can't be opened from here");
                    return;
            }
            MOX::return_screen = from;
            MOX::current_screen = (int16_t)screen;
        }

        void Close(int16_t, const s_args&, s_out&) {
            MOX::current_screen = MOX::return_screen;
        }

        // D25's first argument form (WP3.2b): the Fleets screen's SCRAP button, by ship id.
        // FLT1::Scrap_Ships asks its questions (no payment here, officers aboard, the value) as
        // requests and returns 0 when the player says No to one of them.
        void Scrap_Ships(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            std::vector<int64_t> ids;
            if (!args.Int_List("ships", &ids) || ids.empty() || ids.size() > MAX_SHIPS) {
                out.Fail("bad_args", "ships: a list of ship ids");
                return;
            }
            std::vector<int16_t> ships;
            for (const int64_t id : ids) {
                if (id < 0 || id >= MOX::NUM_SHIPS) {
                    out.Fail("bad_args", "no ship " + std::to_string(id));
                    return;
                }
                const int16_t ship = (int16_t)id;
                if (std::find(ships.begin(), ships.end(), ship) != ships.end()) {
                    out.Fail("bad_args", "ship " + std::to_string(id) + " is listed twice");
                    return;
                }
                if (!Ship_Alive(ship) || MOX::ship[ship].owner != MOX::PLAYER_NUM) {
                    out.Fail("refused", "ship " + std::to_string(id) + " is not one of the player's ships");
                    return;
                }
                // The Fleets screen scraps one stack; Scrap_Ships takes the place from the first ship.
                if (MOX::ship[ship].x != MOX::ship[ships.empty() ? ship : ships[0]].x ||
                    MOX::ship[ship].y != MOX::ship[ships.empty() ? ship : ships[0]].y) {
                    out.Fail("refused", "the ships are not in one place");
                    return;
                }
                ships.push_back(ship);
            }
            const bool done = FLT1::Scrap_Ships(MOX::PLAYER_NUM, ships.data(), (int16_t)ships.size(), 1) != 0;
            if (done) {
                // The galaxy's fleet icons: the Fleets screen rebuilds them after a scrap, and
                // other screens (Fleets itself) reuse what the galaxy built.
                SHIPS::Build_Ship_Icons();
                SHIPS::Set_Ship_Icon_XYs();
            }
            out.Bool("done", done);
        }

        // ---- Queries ----

        void Query_Empire(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const s_player& player = MOX::player[MOX::PLAYER_NUM];
            out.Int("stardate", MOX::stardate);
            out.Int("player", MOX::PLAYER_NUM);
            out.Str("race", Utf8(player.race_name, sizeof(player.race_name)));
            out.Int("color", player.color);
            out.Int("bc", player.bc);
            out.Int("bc_produced", player.bc_produced);
            out.Int("research_produced", player.research_produced);
            out.Int("surplus_food", player.surplus_food);
            out.Int("freighters", player.n_freighters);
            out.Int("command_points", player.command_points);
            out.Int("command_points_used", player.command_points_used);
            out.Int("tax_rate", player.tax_rate);
        }

        void Query_Command_Points(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            s_player& player = MOX::player[MOX::PLAYER_NUM];
            // The legacy screen's own call (COLCALC::Show_Command_Points). It also recomputes the
            // player's command point and maintenance totals, which is harmless while they are
            // current (the read-only guard checks); a pure form is a follow-up (native-bridge.md).
            char text[3000];
            STRBUILD::s_builder builder = STRBUILD::Init(text, sizeof(text));
            COLCALC::Compute_Player_Maintenance(&player, &builder);
            out.Str("title", Utf8(COLCALC::command_summary_msg, 200));
            out.Str("text", Utf8(text, sizeof(text)));
            out.Int("command_points", player.command_points);
            out.Int("command_points_used", player.command_points_used);
        }

        // The player's ships, optionally only those at one star (by its x,y, as the Fleets screen
        // groups them). Ids are what scrap_ships takes.
        void Query_Ships(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t star = -1;
            if (args.Has("star") && (!args.Int("star", &star) || star < 0 || star >= MOX::NUM_STARS)) {
                out.Fail("bad_args", "star: a star id");
                return;
            }
            s_out list = out.Add_List("ships");
            for (int16_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                s_ship_data& ship = MOX::ship[i];
                if (!Ship_Alive(i) || ship.owner != MOX::PLAYER_NUM) {
                    continue;
                }
                const int16_t at = HAROLD::XY_To_Star_Id(ship.x, ship.y);
                if (star >= 0 && at != star) {
                    continue;
                }
                s_out item = list.Append_Object();
                item.Int("id", i);
                item.Str("name", Utf8(ship.d.name, sizeof(ship.d.name)));
                item.Int("status", ship.status);
                item.Int("star", at);
                item.Int("x", ship.x);
                item.Int("y", ship.y);
                item.Int("officer", ship.officer_index);
                item.Bool("captured", ship.d.previous_owner != ship.owner);
                item.Int("scrap_value", maintain::Ship_Scrap_Value(&ship));
            }
        }

        std::vector<s_entry>& Mutable_Table() {
            static std::vector<s_entry> table = {
                {"end_turn", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "", End_Turn},
                {"open", e_kind::command, ANY_SCREEN, 0, "screen: SCREEN_* number", Open},
                {"close", e_kind::command, ANY_SCREEN, 0, "", Close},
                {"scrap_ships", e_kind::command, ANY_SCREEN, 0, "ships: [ship id] (one place)", Scrap_Ships},
                {"empire", e_kind::query, ANY_SCREEN, 1, "", Query_Empire},
                {"command_points", e_kind::query, ANY_SCREEN, 1, "", Query_Command_Points},
                {"ships", e_kind::query, ANY_SCREEN, 1, "star: star id (optional)", Query_Ships},
            };
            static const bool galaxy_added = (internal::Add_Galaxy_Entries(&table), internal::Add_Fleet_Entries(&table), true);
            (void)galaxy_added;
            return table;
        }

        const s_entry* Find(const std::string& name, e_kind kind) {
            for (const s_entry& entry : Mutable_Table()) {
                if (entry.kind == kind && name == entry.name) {
                    return &entry;
                }
            }
            return nullptr;
        }

        // ---- Read-only guard ----

        int s_guard = -1; // -1 not decided
        uint64_t s_violations = 0;
        uint64_t (*s_probe_for_test)() = nullptr;

        struct s_probe {
            uint64_t hash = 0;
            uint32_t seed = 0;
            uint64_t draws = 0;
            bool operator==(const s_probe& other) const {
                return hash == other.hash && seed == other.seed && draws == other.draws;
            }
        };

        s_probe Probe() {
            s_probe probe;
            if (s_probe_for_test != nullptr) {
                probe.hash = s_probe_for_test();
                return probe;
            }
            if (MOX::player == nullptr) {
                return probe; // nothing loaded: nothing to write
            }
            std::string error;
            probe.hash = rules_trace::State_Hash(&error);
            probe.seed = game_random::Get_Random_Seed();
            probe.draws = rng_trace::g_calls;
            return probe;
        }

        // Runs one entry and returns the result text.
        std::string Execute(const s_entry& entry, int16_t screen, uint64_t id, yyjson_val* root, bool with_id) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* result = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, result);
            if (with_id) {
                yyjson_mut_obj_add_uint(doc, result, "id", id);
            }
            s_out out(doc, result);
            const s_args args(root);
            if ((entry.screens & Screen_Bit(screen)) == 0 && entry.screens != ANY_SCREEN) {
                out.Fail("wrong_screen", std::string(entry.name) + " can't run on screen " + std::to_string(screen));
            } else if (entry.kind == e_kind::query && Guard_Enabled()) {
                const s_probe before = Probe();
                entry.run(screen, args, out);
                if (!(Probe() == before)) {
                    ++s_violations;
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "commands: query '%s' changed the game state", entry.name);
                    out.Fail("query_wrote_state", std::string("query ") + entry.name + " changed the game state");
                }
            } else {
                entry.run(screen, args, out);
            }
            if (entry.kind == e_kind::query && !out.Failed()) {
                out.Int("version", entry.version);
            }
            yyjson_mut_obj_add_bool(doc, result, "ok", !out.Failed());
            size_t length = 0;
            char* text = yyjson_mut_write(doc, 0, &length);
            std::string json = text != nullptr ? std::string(text, length) : std::string("{}");
            free(text);
            yyjson_mut_doc_free(doc);
            return json;
        }

        std::string Error(uint64_t id, const char* code, const std::string& message) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* result = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, result);
            yyjson_mut_obj_add_uint(doc, result, "id", id);
            yyjson_mut_obj_add_bool(doc, result, "ok", false);
            yyjson_mut_obj_add_str(doc, result, "error", code);
            yyjson_mut_obj_add_strncpy(doc, result, "message", message.data(), message.size());
            size_t length = 0;
            char* text = yyjson_mut_write(doc, 0, &length);
            std::string json = text != nullptr ? std::string(text, length) : std::string("{}");
            free(text);
            yyjson_mut_doc_free(doc);
            return json;
        }
    } // namespace

    std::string Run(int16_t screen, uint64_t id, const std::string& json) {
        yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
        yyjson_val* root = doc != nullptr ? yyjson_doc_get_root(doc) : nullptr;
        if (!yyjson_is_obj(root)) {
            yyjson_doc_free(doc);
            return Error(id, "bad_json", "not a JSON object");
        }
        const char* verb_text = yyjson_get_str(yyjson_obj_get(root, "verb"));
        const std::string verb = verb_text != nullptr ? verb_text : "";
        std::string result;
        if (verb == "query") {
            const char* what = yyjson_get_str(yyjson_obj_get(root, "what"));
            const s_entry* entry = Find(what != nullptr ? what : "", e_kind::query);
            result = entry != nullptr ? Execute(*entry, screen, id, root, true)
                                      : Error(id, "unknown_query", std::string("no query '") + (what ? what : "") + "'");
        } else {
            const s_entry* entry = Find(verb, e_kind::command);
            result = entry != nullptr ? Execute(*entry, screen, id, root, true)
                                      : Error(id, "unknown_verb", "no command '" + verb + "'");
        }
        yyjson_doc_free(doc);
        return result;
    }

    std::string Run_Query(int16_t screen, const std::string& what) {
        const s_entry* entry = Find(what, e_kind::query);
        if (entry == nullptr) {
            return {};
        }
        yyjson_doc* doc = yyjson_read("{}", 2, 0);
        std::string result = Execute(*entry, screen, 0, yyjson_doc_get_root(doc), false);
        yyjson_doc_free(doc);
        return result;
    }

    const std::vector<s_entry>& Table() {
        return Mutable_Table();
    }

    bool Guard_Enabled() {
        if (s_guard < 0) {
#if defined(NDEBUG)
            const char* value = antares::Get_Env("QUERY_GUARD");
            s_guard = value != nullptr && value[0] != '\0' && strcmp(value, "0") != 0 ? 1 : 0;
#else
            s_guard = 1;
#endif
        }
        return s_guard == 1;
    }

    uint64_t Guard_Violations() {
        return s_violations;
    }

    void Set_Guard_For_Test(bool on) {
        s_guard = on ? 1 : 0;
    }

    void Set_State_Probe_For_Test(uint64_t (*probe)()) {
        s_probe_for_test = probe;
    }

    void Register_For_Test(const s_entry& entry) {
        std::vector<s_entry>& table = Mutable_Table();
        for (s_entry& existing : table) {
            if (existing.kind == entry.kind && strcmp(existing.name, entry.name) == 0) {
                existing = entry;
                return;
            }
        }
        table.push_back(entry);
    }

} // namespace antares::commands

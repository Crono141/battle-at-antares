#include "pch.h"

#include "antares/native.h"

#include "antares/host.h"
#include "config/text_encoding.h"
#include "miniz.h"
#include "yyjson.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>

namespace antares::native {
    namespace {
        std::atomic<uint64_t> s_claimed_screens{0}; // bit n: SCREEN_* n
        std::atomic<bool> s_requests_claimed{false};
        std::atomic<int16_t> s_native_screen{-1};
        std::atomic<uint64_t> s_native_visit{0};
        std::atomic<uint64_t> s_leave_frame{0};

        struct s_command {
            uint64_t id = 0;
            std::string json;
        };

        std::mutex s_mutex; // guards everything below
        uint64_t s_next_id = 1;
        std::deque<s_command> s_commands;
        std::deque<std::string> s_results;
        std::deque<s_request> s_requests;
        uint64_t s_waiting_request = 0;
        bool s_answered = false;
        int32_t s_answer = 0;
        std::vector<std::string> s_packs;
        uint64_t s_packs_generation = 0;

        // Game thread only.
        std::string s_packages_key;

        bool Claimed(int16_t screen) {
            return screen >= 0 && screen < 64 && ((s_claimed_screens.load() >> screen) & 1u) != 0;
        }

        std::string Utf8(const char* text) {
            std::string decoded;
            if (text == nullptr) {
                return decoded;
            }
            config::Decode_Cp850_To_Utf8(text, strlen(text), &decoded);
            std::string clean;
            clean.reserve(decoded.size());
            for (const char c : decoded) {
                // The game's text uses control bytes for colour and layout; drop them.
                if ((unsigned char)c >= 0x20 || c == '\n') {
                    clean.push_back(c);
                }
            }
            return clean;
        }

        // Keeps the game alive while it waits on the frontend: exit requests (throws on quit),
        // sound streaming and network traffic, as Get_Input loops do through Mox_Update.
        void Tick() {
            RUSS::Mox_Update();
            SDL_Delay(5);
        }

        // Input the platform thread queued for the legacy screen while a native view was up.
        void Drop_Legacy_Input() {
            while (key::Keyboard_Status() != 0) {
                (void)key::Read_Key();
            }
            fields::Clear_Mouse_Buffer();
        }

        // ---- Results ----

        struct s_writer {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* root = nullptr;

            explicit s_writer(uint64_t id, bool ok) {
                root = yyjson_mut_obj(doc);
                yyjson_mut_doc_set_root(doc, root);
                yyjson_mut_obj_add_uint(doc, root, "id", id);
                yyjson_mut_obj_add_bool(doc, root, "ok", ok);
            }
            ~s_writer() {
                yyjson_mut_doc_free(doc);
            }
            s_writer(const s_writer&) = delete;
            s_writer& operator=(const s_writer&) = delete;

            void Int(const char* key, int64_t value) {
                yyjson_mut_obj_add_int(doc, root, key, value);
            }
            void Str(const char* key, const std::string& value) {
                yyjson_mut_obj_add_strncpy(doc, root, key, value.data(), value.size());
            }
            std::string Text() const {
                size_t length = 0;
                char* text = yyjson_mut_write(doc, 0, &length);
                std::string out = text != nullptr ? std::string(text, length) : std::string("{}");
                free(text);
                return out;
            }
        };

        std::string Error(uint64_t id, const char* message) {
            s_writer out(id, false);
            out.Str("error", message);
            return out.Text();
        }

        // ---- Commands ----

        // The screens the native galaxy may open through the legacy fallback, with the setup the
        // legacy galaxy does before it leaves for them (mainscr_main.cpp, Main_Screen).
        bool Open_Screen(int16_t from, int16_t screen) {
            switch (screen) {
                case SCREEN_COLONY_SUMMARY:
                case SCREEN_INFO:
                case SCREEN_RACE:
                case SCREEN_PLANET_SUMMARY:
                case SCREEN_SHOW_COMMAND_POINTS:
                    break;
                case SCREEN_FLEET:
                    MAINSCR::officer_screen_return_screen = SCREEN_FLEET;
                    break;
                case SCREEN_GAME:
                    MOX::screen_data = 0;
                    break;
                case SCREEN_TECH_CHANGE:
                    MAINSCR::clear_fleet_fields = 0;
                    break;
                case SCREEN_OFFICERS:
                    MOX::temp_star_handle = MOX::officer_scrn_type == LEADER_TYPE_SHIP
                        ? MAINSCR::Get_Ship_Stack_For_Officers_Screen()
                        : MAINSCR::Get_Star_Id_For_Officers_Screen();
                    MAINSCR::officer_screen_return_screen = (int8_t)from;
                    break;
                default:
                    return false;
            }
            MOX::return_screen = from;
            MOX::current_screen = screen;
            return true;
        }

        // What the legacy galaxy does when the turn button is pressed (Main_Screen, next_button).
        void End_Turn() {
            RUSS::Assert_Settings();
            MOX::auto_turn_exit = 0;
            MAINSCR::next_turn_hit = 1;
            MOX::global_exit_field = -1000;
            int16_t exit_flag = 0;
            MAINSCR::Do_Next_Turn(&exit_flag, 0);
        }

        std::string Query(uint64_t id, const std::string& what) {
            if (MOX::player == nullptr || MOX::PLAYER_NUM < 0 || MOX::PLAYER_NUM >= MOX::NUM_PLAYERS) {
                return Error(id, "no game");
            }
            s_player& player = MOX::player[MOX::PLAYER_NUM];
            if (what == "empire") {
                s_writer out(id, true);
                out.Int("stardate", MOX::stardate);
                out.Int("player", MOX::PLAYER_NUM);
                out.Str("race", Utf8(player.race_name));
                out.Int("color", player.color);
                out.Int("bc", player.bc);
                out.Int("bc_produced", player.bc_produced);
                out.Int("research_produced", player.research_produced);
                out.Int("surplus_food", player.surplus_food);
                out.Int("freighters", player.n_freighters);
                out.Int("command_points", player.command_points);
                out.Int("command_points_used", player.command_points_used);
                out.Int("tax_rate", player.tax_rate);
                return out.Text();
            }
            if (what == "command_points") {
                // The same call the legacy screen makes (COLCALC::Show_Command_Points), which
                // also refreshes the player's command point totals.
                char text[3000];
                STRBUILD::s_builder builder = STRBUILD::Init(text, sizeof(text));
                COLCALC::Compute_Player_Maintenance(&player, &builder);
                s_writer out(id, true);
                out.Str("title", Utf8(COLCALC::command_summary_msg));
                out.Str("text", Utf8(text));
                out.Int("command_points", player.command_points);
                out.Int("command_points_used", player.command_points_used);
                return out.Text();
            }
            return Error(id, "unknown query");
        }

        std::string Run_Command(int16_t screen, uint64_t id, const std::string& json) {
            yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
            if (doc == nullptr) {
                return Error(id, "bad json");
            }
            yyjson_val* root = yyjson_doc_get_root(doc);
            const char* verb_text = yyjson_get_str(yyjson_obj_get(root, "verb"));
            const std::string verb = verb_text != nullptr ? verb_text : "";
            std::string result;
            if (verb == "query") {
                const char* what = yyjson_get_str(yyjson_obj_get(root, "what"));
                result = Query(id, what != nullptr ? what : "");
            } else if (verb == "end_turn") {
                if (screen != SCREEN_MAIN) {
                    result = Error(id, "end_turn needs the galaxy");
                } else {
                    End_Turn();
                    result = s_writer(id, true).Text();
                }
            } else if (verb == "open") {
                yyjson_val* target = yyjson_obj_get(root, "screen");
                const int16_t next = yyjson_is_int(target) ? (int16_t)yyjson_get_int(target) : SCREEN_UNKNOWN;
                result = Open_Screen(screen, next) ? s_writer(id, true).Text()
                                                   : Error(id, "screen can't be opened from here");
            } else if (verb == "close") {
                MOX::current_screen = MOX::return_screen;
                result = s_writer(id, true).Text();
            } else {
                result = Error(id, "unknown verb");
            }
            yyjson_doc_free(doc);
            return result;
        }

        // The native galaxy is offered only where the legacy one does nothing more than wait for
        // input: single player, and not on the first turn's home star renaming.
        bool Native_Allowed(int16_t screen) {
            if (screen != SCREEN_MAIN) {
                return true;
            }
            if (MOX::game_type != 0 || MOX::player == nullptr) {
                return false;
            }
            if (MOX::given_chance_to_rename_home_star[MOX::PLAYER_NUM] == 0 && MOX::stardate == 35000) {
                return false;
            }
            return MOX::player[MOX::PLAYER_NUM].surrender_to == -1;
        }

        // What Main_Screen does on entry that isn't drawing (mainscr_main.cpp, Main_Screen): star
        // owners for the map, the fade flags the next screens read, and the message about a
        // loaded save's packages, which becomes a request.
        void Enter_Galaxy() {
            MAINSCR::Update_Star_Owners();
            const std::string message = SAVEGAME::Consume_Loaded_Config_Message();
            if (!message.empty()) {
                int16_t ignored = -1;
                if (!User_Box(0, message.c_str(), &ignored)) {
                    GENDRAW::Message_Box(message.c_str());
                }
            }
            MAINSCR2::main_screen_fade_in_flag = 1;
            MOX::at_least_one_tactical_combat = 0;
        }

        // What Main_Screen resets when it leaves, so legacy screens find the galaxy state they
        // expect (mainscr_main.cpp, Main_Screen cleanup).
        void Leave_Galaxy() {
            MAINSCR::Set_Reload_Fleet_Box_Palette_Flag();
            MOX::first_fleet_movement_icon = 0;
            MOX::eta_star_id = -1;
            MOX::stencil_box_id = -1;
            MOX::draw_main_screen_stencilled = 0;
            SYS::Clear_System_Display_Loaded_Flag();
            MOX::animated_star = -1;
            MOX::draw_main_screen_to_back = 1;
        }

        // ---- Packages ----

        // Godot resolves pack paths itself, so hand it absolute ones (the game runs in its folder).
        std::string Absolute(const std::string& path) {
            if (!path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'))) {
                return path;
            }
            char* cwd = SDL_GetCurrentDirectory(); // ends with a separator
            std::string out = (cwd != nullptr ? std::string(cwd) : std::string()) + path;
            SDL_free(cwd);
            return out;
        }

        bool Is_Pack(const std::string& name) {
            const auto ends = [&](const char* suffix) {
                const size_t n = strlen(suffix);
                return name.size() > n && SDL_strcasecmp(name.c_str() + name.size() - n, suffix) == 0;
            };
            return ends(".pck") || ends(".zip");
        }

        SDL_EnumerationResult SDLCALL Collect_Pack(void* user, const char* dir, const char* name) {
            auto* out = static_cast<std::vector<std::string>*>(user);
            if (name != nullptr && Is_Pack(name)) {
                out->push_back(std::string(dir) + name);
            }
            return SDL_ENUM_CONTINUE;
        }

        // A zipped package's godot/ packs are extracted to cache/godot/<package>/, since Godot
        // loads packs from files.
        void Extract_Archive_Packs(const vfs::s_package_source& source, std::vector<std::string>* out) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, source.source_path.c_str(), 0)) {
                return;
            }
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < count; ++i) {
                char name[512];
                mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
                const std::string entry = name;
                if (entry.rfind("godot/", 0) != 0 || entry.find('/', 6) != std::string::npos || !Is_Pack(entry)) {
                    continue;
                }
                const std::string dir = "cache/godot/" + source.package_id + "/";
                (void)SDL_CreateDirectory(dir.c_str());
                const std::string path = dir + entry.substr(6);
                mz_zip_archive_file_stat stat{};
                SDL_PathInfo info{};
                const bool fresh = mz_zip_reader_file_stat(&zip, i, &stat) && SDL_GetPathInfo(path.c_str(), &info) &&
                    (mz_uint64)info.size == stat.m_uncomp_size;
                if (fresh || mz_zip_reader_extract_to_file(&zip, i, path.c_str(), 0)) {
                    out->push_back(path);
                }
            }
            mz_zip_reader_end(&zip);
        }
    } // namespace

    void Claim_Screen(int16_t screen, bool claimed) {
        if (screen < 0 || screen >= 64) {
            return;
        }
        const uint64_t bit = uint64_t{1} << screen;
        if (claimed) {
            s_claimed_screens.fetch_or(bit);
        } else {
            s_claimed_screens.fetch_and(~bit);
        }
    }

    void Claim_Requests(bool claimed) {
        s_requests_claimed = claimed;
    }

    uint64_t Send_Command(const std::string& json) {
        std::lock_guard<std::mutex> lock(s_mutex);
        const uint64_t id = s_next_id++;
        s_commands.push_back({id, json});
        return id;
    }

    bool Take_Result(std::string* json) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_results.empty()) {
            return false;
        }
        *json = std::move(s_results.front());
        s_results.pop_front();
        return true;
    }

    int16_t Native_Screen() {
        return s_native_screen;
    }

    uint64_t Native_Visit() {
        return s_native_visit;
    }

    bool Request_Pending() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_waiting_request != 0;
    }

    uint64_t Leave_Frame_Generation() {
        return s_leave_frame;
    }

    bool Take_Request(s_request* request) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_requests.empty()) {
            return false;
        }
        *request = std::move(s_requests.front());
        s_requests.pop_front();
        return true;
    }

    void Answer_Request(uint64_t id, int32_t value) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (id == s_waiting_request) {
            s_answered = true;
            s_answer = value;
        }
    }

    std::vector<std::string> Resource_Packs(uint64_t* generation) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (generation != nullptr) {
            *generation = s_packs_generation;
        }
        return s_packs;
    }

    void Update_Packages() {
        if (!host::Active()) {
            return;
        }
        const std::vector<vfs::s_package_source> sources = vfs::Package_Sources();
        std::string key;
        for (const auto& source : sources) {
            key += source.package_id + "=" + source.source_path + ";";
        }
        if (key == s_packages_key) {
            return;
        }
        s_packages_key = key;
        std::vector<std::string> packs;
        // Lowest priority first: sources are highest first.
        for (auto it = sources.rbegin(); it != sources.rend(); ++it) {
            std::vector<std::string> found;
            if (it->is_archive) {
                (void)SDL_CreateDirectory("cache/godot");
                Extract_Archive_Packs(*it, &found);
            } else {
                const std::string dir = it->source_path + "godot/";
                (void)SDL_EnumerateDirectory(dir.c_str(), Collect_Pack, &found);
            }
            std::sort(found.begin(), found.end());
            for (std::string& path : found) {
                path = Absolute(path);
            }
            packs.insert(packs.end(), found.begin(), found.end());
        }
        std::lock_guard<std::mutex> lock(s_mutex);
        if (packs != s_packs || s_packs_generation == 0) {
            s_packs = std::move(packs);
            ++s_packs_generation;
        }
    }

    bool Run_Screen(int16_t screen) {
        if (!host::Active()) {
            return false;
        }
        if (!Claimed(screen) || !Native_Allowed(screen)) {
            return false;
        }
        if (screen == SCREEN_MAIN) {
            Enter_Galaxy();
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "native: screen %d", (int)screen);
        host::On_Publish();
        ++s_native_visit;
        s_native_screen = screen;
        uint32_t ticks = 0;
        // Commands that arrived for an earlier native screen still run: they were sent while the
        // frontend showed a native view, and each checks the screen it needs.
        while (MOX::current_screen == screen && Claimed(screen)) {
            Tick();
            std::deque<s_command> commands;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                commands.swap(s_commands);
            }
            bool changed = false;
            while (!commands.empty()) {
                s_command command = std::move(commands.front());
                commands.pop_front();
                if (MOX::current_screen != screen) {
                    // An earlier command left this screen: the rest wait for the next one.
                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_commands.insert(s_commands.begin(), commands.begin(), commands.end());
                    s_commands.push_front(std::move(command));
                    break;
                }
                std::string result = Run_Command(screen, command.id, command.json);
                changed = true;
                std::lock_guard<std::mutex> lock(s_mutex);
                s_results.push_back(std::move(result));
            }
            if (changed || ++ticks % 20 == 0) {
                host::On_Publish();
            }
        }
        if (screen == SCREEN_MAIN) {
            Leave_Galaxy();
        }
        Drop_Legacy_Input();
        s_leave_frame = host::Frame_Generation(); // before the screen, so the frontend never misses it
        s_native_screen = -1;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "native: screen %d leaves for %d", (int)screen,
            (int)MOX::current_screen);
        return true;
    }

    bool Wants_Screen(int16_t screen) {
        return host::Active() && Claimed(screen) && Native_Allowed(screen);
    }

    bool User_Box(int16_t type, const char* message, int16_t* result) {
        if (!host::Active() || !s_requests_claimed) {
            return false;
        }
        const char* kind = type == 0 ? "message" : type == 1 ? "confirm" : type == 3 ? "warning" : nullptr;
        if (kind == nullptr) {
            return false; // text boxes with a title and timed boxes stay legacy for now
        }
        uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            id = s_next_id++;
            s_waiting_request = id;
            s_answered = false;
            s_requests.push_back({id, kind, Utf8(message)});
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "native: request %llu (%s)", (unsigned long long)id, kind);
        while (true) {
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if (s_answered) {
                    s_waiting_request = 0;
                    *result = type == 1 ? (int16_t)(s_answer != 0 ? 1 : 0) : (int16_t)-1;
                    break;
                }
            }
            Tick();
        }
        Drop_Legacy_Input();
        return true;
    }

    std::string Run_Command_For_Test(int16_t screen, const std::string& json) {
        return Run_Command(screen, 0, json);
    }

} // namespace antares::native

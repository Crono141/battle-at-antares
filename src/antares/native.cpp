#include "pch.h"

#include "antares/native.h"

#include "antares/commands.h"
#include "antares/host.h"
#include "antares/input_trace.h"
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
        std::atomic<uint32_t> s_claimed_kinds{0};   // bits: e_request_kind
        std::atomic<bool> s_control_attached{false};
        std::atomic<int16_t> s_native_screen{-1};
        std::atomic<uint64_t> s_native_visit{0};
        std::atomic<uint64_t> s_leave_frame{0};
        std::atomic<uint64_t> s_leave_polls{0};

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
        std::vector<std::string> s_package_ids; // highest priority first
        uint64_t s_packs_generation = 0;
        std::deque<std::string> s_snapshots;
        struct s_sprite_request {
            uint64_t id = 0;
            std::string lbx;
            int32_t entry = 0;
        };
        std::deque<s_sprite_request> s_sprite_requests;
        std::deque<std::pair<uint64_t, sprites::s_sprite>> s_sprites;

        // Game thread only.
        std::string s_packages_key;
        struct s_watch {
            std::string what;
            std::string last; // the last posted answer
        };
        std::vector<s_watch> s_watches;
        uint64_t s_snapshot_seq = 0;

        enum e_request_kind : uint32_t { KIND_MESSAGE = 1,
            KIND_CONFIRM = 2,
            KIND_WARNING = 4,
            KIND_CHOOSE = 8 };

        uint32_t Kind_Bit(const std::string& kind) {
            return kind == "message" ? KIND_MESSAGE
                : kind == "confirm"  ? KIND_CONFIRM
                : kind == "warning"  ? KIND_WARNING
                : kind == "choose"   ? KIND_CHOOSE
                                     : 0;
        }

        // A frontend is attached: the Godot host, or the control channel in the SDL build.
        bool Frontend() {
            return host::Active() || s_control_attached;
        }

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

        // ---- Commands ----

        std::string Json_Quote(const std::string& text) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_doc_set_root(doc, yyjson_mut_strncpy(doc, text.data(), text.size()));
            size_t length = 0;
            char* out = yyjson_mut_write(doc, 0, &length);
            std::string quoted = out != nullptr ? std::string(out, length) : std::string("\"\"");
            free(out);
            yyjson_mut_doc_free(doc);
            return quoted;
        }

        std::string Bridge_Result(uint64_t id, bool ok, const char* error = nullptr) {
            std::string out = "{\"id\":" + std::to_string(id) + ",\"ok\":" + (ok ? "true" : "false");
            if (error != nullptr) {
                out += ",\"error\":" + Json_Quote(error);
            }
            return out + "}";
        }

        // The bridge verbs (watch, unwatch) and then the shared table (antares/commands.h).
        std::string Run_Command(int16_t screen, uint64_t id, const std::string& json) {
            yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
            yyjson_val* root = doc != nullptr ? yyjson_doc_get_root(doc) : nullptr;
            const char* verb = yyjson_get_str(yyjson_obj_get(root, "verb"));
            const char* what = yyjson_get_str(yyjson_obj_get(root, "what"));
            std::string result;
            if (verb != nullptr && (strcmp(verb, "watch") == 0 || strcmp(verb, "unwatch") == 0)) {
                const bool watch = strcmp(verb, "watch") == 0;
                const std::string name = what != nullptr ? what : "";
                const auto it = std::find_if(s_watches.begin(), s_watches.end(), [&](const s_watch& w) { return w.what == name; });
                if (watch && commands::Run_Query(screen, name).empty()) {
                    result = Bridge_Result(id, false, "unknown_query");
                } else {
                    if (watch && it == s_watches.end()) {
                        s_watches.push_back({name, {}});
                    } else if (watch) {
                        it->last.clear(); // post the current answer again
                    } else if (it != s_watches.end()) {
                        s_watches.erase(it);
                    }
                    result = Bridge_Result(id, true);
                }
            } else {
                result = commands::Run(screen, id, json);
            }
            yyjson_doc_free(doc);
            return result;
        }

        // Re-runs the watched queries and posts the ones whose answer changed.
        void Refresh_Snapshots(int16_t screen) {
            for (s_watch& watch : s_watches) {
                std::string answer = commands::Run_Query(screen, watch.what);
                if (answer.empty() || answer == watch.last) {
                    continue;
                }
                watch.last = answer;
                std::string snapshot = "{\"snapshot\":" + Json_Quote(watch.what) + ",\"seq\":" +
                    std::to_string(++s_snapshot_seq) + ",\"result\":" + answer + "}";
                std::lock_guard<std::mutex> lock(s_mutex);
                s_snapshots.push_back(std::move(snapshot));
            }
        }

        // Exports queued sprites, for at most `budget_ms` (the first one always runs).
        void Serve_Sprites(uint64_t budget_ms) {
            const uint64_t start = SDL_GetTicks();
            while (true) {
                s_sprite_request request;
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    if (s_sprite_requests.empty()) {
                        return;
                    }
                    request = std::move(s_sprite_requests.front());
                    s_sprite_requests.pop_front();
                }
                sprites::s_sprite sprite;
                if (!sprites::Export(request.lbx, request.entry, &sprite)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "native: sprite %s %d: %s", request.lbx.c_str(), (int)request.entry, sprite.error.c_str());
                }
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_sprites.emplace_back(request.id, std::move(sprite));
                }
                if (SDL_GetTicks() - start >= budget_ms) {
                    return;
                }
            }
        }

        // ---- Requests ----

        // Posts a request (the fields after "id" and "kind", as JSON text without braces) and
        // waits on the game thread for its answer, keeping sound, network and exit checks alive.
        int32_t Ask(const char* kind, const char* text, const std::string& fields) {
            uint64_t id = 0;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                id = s_next_id++;
                s_waiting_request = id;
                s_answered = false;
                const std::string utf8 = Utf8(text);
                std::string json = "{\"id\":" + std::to_string(id) + ",\"kind\":\"" + kind + "\",\"text\":" +
                    Json_Quote(utf8) + (fields.empty() ? "" : ",") + fields + "}";
                s_requests.push_back({id, kind, utf8, std::move(json)});
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "native: request %llu (%s)", (unsigned long long)id, kind);
            int32_t answer = 0;
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    if (s_answered) {
                        s_waiting_request = 0;
                        answer = s_answer;
                        break;
                    }
                }
                Tick();
            }
            Drop_Legacy_Input();
            return answer;
        }

        bool Claimed_Kind(uint32_t bit) {
            return Frontend() && (s_claimed_kinds.load() & bit) != 0;
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
        // owners and fleet icons for the map and the fade flags the next screens read. The message
        // about a loaded save's packages comes once the galaxy is native (Run_Screen).
        void Enter_Galaxy() {
            MAINSCR::Update_Star_Owners();
            // The fleet icons, as Main_Screen builds them on entry: Fleets reuses them.
            SHIPS::Build_Ship_Icons();
            SHIPS::Set_Ship_Icon_XYs();
            MAINSCR2::main_screen_fade_in_flag = 1;
            MOX::at_least_one_tactical_combat = 0;
            // The galaxy palette, as Main_Screen loads it on entry: the popups that open over the
            // galaxy (research, the game menu) draw with whatever palette is loaded, and without
            // this they kept the last legacy screen's (owner, 2026-11-06: research came up dark).
            fonts::Load_Palette(0, 0, 0xad);
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

    void Claim_Requests(const std::vector<std::string>& kinds) {
        uint32_t bits = 0;
        for (const std::string& kind : kinds) {
            bits |= Kind_Bit(kind);
        }
        s_claimed_kinds = bits;
    }

    void Attach_Control(bool attached) {
        s_control_attached = attached;
    }

    bool Control_Attached() {
        return s_control_attached;
    }

    bool Take_Snapshot(std::string* json) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_snapshots.empty()) {
            return false;
        }
        *json = std::move(s_snapshots.front());
        s_snapshots.pop_front();
        return true;
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

    uint64_t Leave_Polls() {
        return s_leave_polls;
    }

    uint64_t Polls() {
        return input_trace::Polls();
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

    uint64_t Request_Sprite(const std::string& lbx, int32_t entry) {
        std::lock_guard<std::mutex> lock(s_mutex);
        const uint64_t id = s_next_id++;
        s_sprite_requests.push_back({id, lbx, entry});
        return id;
    }

    bool Take_Sprite(uint64_t* id, sprites::s_sprite* sprite) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_sprites.empty()) {
            return false;
        }
        *id = s_sprites.front().first;
        *sprite = std::move(s_sprites.front().second);
        s_sprites.pop_front();
        return true;
    }

    std::vector<std::string> Resource_Packs(uint64_t* generation) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (generation != nullptr) {
            *generation = s_packs_generation;
        }
        return s_packs;
    }

    std::vector<std::string> Packages() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_package_ids;
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
        std::vector<std::string> ids;
        for (const auto& source : sources) {
            ids.push_back(source.package_id);
        }
        std::lock_guard<std::mutex> lock(s_mutex);
        if (packs != s_packs || ids != s_package_ids || s_packs_generation == 0) {
            s_packs = std::move(packs);
            s_package_ids = std::move(ids);
            ++s_packs_generation;
        }
    }

    bool Galaxy_Will_Be_Native() {
        return Frontend() && Claimed(SCREEN_MAIN) && Native_Allowed(SCREEN_MAIN);
    }

    bool Run_Screen(int16_t screen) {
        if (!Frontend()) {
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
        Refresh_Snapshots(screen);
        // As Main_Screen on entry, the message about a loaded save's packages, but once the native
        // galaxy is up and has had its first commands (its watches and art) for a moment: asked at
        // once, it showed over the last legacy frame and then an empty map (owner, 2026-11-11).
        std::string entry_message = screen == SCREEN_MAIN ? SAVEGAME::Consume_Loaded_Config_Message() : std::string();
        constexpr uint32_t k_entry_message_ticks = 100; // about half a second
        uint32_t ticks = 0;
        // Commands that arrived for an earlier native screen still run: they were sent while the
        // frontend showed a native view, and each checks the screen it needs.
        while (MOX::current_screen == screen && Claimed(screen)) {
            Tick();
            Serve_Sprites(20);
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
                if (MOX::current_screen == screen) {
                    Refresh_Snapshots(screen);
                }
            }
            if (!entry_message.empty() && ticks >= k_entry_message_ticks && MOX::current_screen == screen) {
                const std::string message = std::move(entry_message);
                entry_message.clear();
                GENDRAW::Message_Box(message.c_str()); // a "message" request when claimed
            }
        }
        if (screen == SCREEN_MAIN) {
            Leave_Galaxy();
        }
        Drop_Legacy_Input();
        s_leave_polls = input_trace::Polls();
        s_leave_frame = host::Frame_Generation(); // before the screen, so the frontend never misses it
        s_native_screen = -1;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "native: screen %d leaves for %d", (int)screen, (int)MOX::current_screen);
        return true;
    }

    bool Wants_Screen(int16_t screen) {
        return Frontend() && Claimed(screen) && Native_Allowed(screen);
    }

    bool Message(const char* title, const char* text, uint16_t timeout_ticks) {
        if (!Claimed_Kind(KIND_MESSAGE)) {
            return false;
        }
        std::string fields;
        if (title != nullptr) {
            fields += "\"title\":" + Json_Quote(Utf8(title));
        }
        if (timeout_ticks != 0) {
            fields += std::string(fields.empty() ? "" : ",") + "\"timeout_ticks\":" + std::to_string(timeout_ticks);
        }
        (void)Ask("message", text, fields);
        MOX::FULL_DRAW_SCREEN_FLAG = 1; // as Do_Text_Box leaves it
        return true;
    }

    bool Confirm(const char* text, int16_t* answer) {
        if (!Claimed_Kind(KIND_CONFIRM)) {
            return false;
        }
        *answer = Ask("confirm", text, "") != 0 ? 1 : 0;
        MOX::FULL_DRAW_SCREEN_FLAG = 1; // as Confirmation_Box leaves it
        return true;
    }

    bool Warning(const char* text) {
        if (!Claimed_Kind(KIND_WARNING)) {
            return false;
        }
        (void)Ask("warning", text, "");
        return true;
    }

    bool Choose(const char* title, const char* list, int32_t item_size, const uint8_t* flags, int8_t* selected, int16_t* answer) {
        if (!Claimed_Kind(KIND_CHOOSE) || list == nullptr || item_size <= 0) {
            return false;
        }
        std::string options;
        int16_t count = 0;
        for (const char* item = list; *item != '\0'; item += item_size, ++count) {
            const bool enabled = flags == nullptr || flags[count] != 0;
            options += std::string(count == 0 ? "" : ",") + "{\"text\":" +
                Json_Quote(Utf8(std::string(item, strnlen(item, (size_t)item_size)).c_str())) +
                ",\"enabled\":" + (enabled ? "true" : "false") + "}";
        }
        std::string fields = "\"options\":[" + options + "]";
        if (selected != nullptr) {
            fields += ",\"selected\":" + std::to_string(*selected);
        }
        while (true) {
            const int32_t value = Ask("choose", title, fields);
            // The legacy list ignores clicks on disabled items: ask again.
            if (value < 0 || value >= count) {
                *answer = -1;
                return true;
            }
            if (flags == nullptr || flags[value] != 0) {
                *answer = (int16_t)value;
                if (selected != nullptr) {
                    *selected = (int8_t)value;
                }
                return true;
            }
        }
    }

    void Refresh_Snapshots_For_Test(int16_t screen) {
        Refresh_Snapshots(screen);
    }

    std::string Run_Command_For_Test(int16_t screen, const std::string& json) {
        return Run_Command(screen, 0, json);
    }

} // namespace antares::native

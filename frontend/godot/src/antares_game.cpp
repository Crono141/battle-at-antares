#include "antares_game.h"

#include "antares/host.h"
#include "antares/native.h"
#include "command_line.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#if defined(_WIN32)
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

int antares_hosted_main(int argc, char** argv);

namespace godot {

    namespace {
        // Godot key to SDL keycode, as the SDL build's window would have reported it.
        uint32_t To_Sdl_Key(Key key) {
            switch (key) {
                case KEY_ESCAPE: return SDLK_ESCAPE;
                case KEY_ENTER:
                case KEY_KP_ENTER: return SDLK_RETURN;
                case KEY_BACKSPACE: return SDLK_BACKSPACE;
                case KEY_TAB: return SDLK_TAB;
                case KEY_DELETE: return SDLK_DELETE;
                case KEY_INSERT: return SDLK_INSERT;
                case KEY_HOME: return SDLK_HOME;
                case KEY_END: return SDLK_END;
                case KEY_PAGEUP: return SDLK_PAGEUP;
                case KEY_PAGEDOWN: return SDLK_PAGEDOWN;
                case KEY_LEFT: return SDLK_LEFT;
                case KEY_RIGHT: return SDLK_RIGHT;
                case KEY_UP: return SDLK_UP;
                case KEY_DOWN: return SDLK_DOWN;
                default: break;
            }
            if (key >= KEY_F1 && key <= KEY_F12) {
                return SDLK_F1 + (uint32_t)(key - KEY_F1);
            }
            if (key >= KEY_A && key <= KEY_Z) {
                return (uint32_t)key + ('a' - 'A');
            }
            if (key >= 0x20 && key <= 0x7E) {
                return (uint32_t)key;
            }
            return 0;
        }

        uint16_t To_Sdl_Mod(bool shift, bool alt, bool ctrl) {
            return (uint16_t)((shift ? SDL_KMOD_LSHIFT : 0) | (alt ? SDL_KMOD_LALT : 0) | (ctrl ? SDL_KMOD_LCTRL : 0));
        }
    } // namespace

    AntaresGame::~AntaresGame() {
        stop();
    }

    void AntaresGame::_bind_methods() {
        ClassDB::bind_method(D_METHOD("start"), &AntaresGame::start);
        ClassDB::bind_method(D_METHOD("stop"), &AntaresGame::stop);
        ClassDB::bind_method(D_METHOD("is_running"), &AntaresGame::is_running);
        ClassDB::bind_method(D_METHOD("set_game_dir", "dir"), &AntaresGame::set_game_dir);
        ClassDB::bind_method(D_METHOD("get_game_dir"), &AntaresGame::get_game_dir);
        ClassDB::bind_method(D_METHOD("set_args", "args"), &AntaresGame::set_args);
        ClassDB::bind_method(D_METHOD("get_args"), &AntaresGame::get_args);
        ClassDB::bind_method(D_METHOD("set_autostart", "value"), &AntaresGame::set_autostart);
        ClassDB::bind_method(D_METHOD("get_autostart"), &AntaresGame::get_autostart);
        ClassDB::bind_method(D_METHOD("get_stars"), &AntaresGame::get_stars);
        ClassDB::bind_method(D_METHOD("get_galaxy_generation"), &AntaresGame::get_galaxy_generation);
        ClassDB::bind_method(D_METHOD("get_stats"), &AntaresGame::get_stats);
        ClassDB::bind_method(D_METHOD("send_key", "godot_keycode", "shift", "alt", "ctrl"), &AntaresGame::send_key);
        ClassDB::bind_method(D_METHOD("request_game_quit"), &AntaresGame::request_game_quit);
        ClassDB::bind_method(D_METHOD("claim_screen", "screen", "claimed"), &AntaresGame::claim_screen);
        ClassDB::bind_method(D_METHOD("claim_requests", "kinds"), &AntaresGame::claim_requests);
        ClassDB::bind_method(D_METHOD("send_command", "command"), &AntaresGame::send_command);
        ClassDB::bind_method(D_METHOD("get_native_screen"), &AntaresGame::get_native_screen);
        ClassDB::bind_method(D_METHOD("get_native_visit"), &AntaresGame::get_native_visit);
        ClassDB::bind_method(D_METHOD("answer_request", "id", "value"), &AntaresGame::answer_request);
        ClassDB::bind_method(D_METHOD("get_resource_packs"), &AntaresGame::get_resource_packs);
        ClassDB::bind_method(D_METHOD("get_frame_image"), &AntaresGame::get_frame_image);
        ClassDB::bind_method(D_METHOD("request_sprite", "lbx", "entry"), &AntaresGame::request_sprite);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "game_dir", PROPERTY_HINT_GLOBAL_DIR), "set_game_dir", "get_game_dir");
        ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "args"), "set_args", "get_args");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autostart"), "set_autostart", "get_autostart");
        ADD_SIGNAL(MethodInfo("galaxy_changed"));
        ADD_SIGNAL(MethodInfo("game_exited"));
        ADD_SIGNAL(MethodInfo("native_screen_changed", PropertyInfo(Variant::INT, "screen")));
        ADD_SIGNAL(MethodInfo("command_result", PropertyInfo(Variant::INT, "id"), PropertyInfo(Variant::DICTIONARY, "result")));
        ADD_SIGNAL(MethodInfo("request", PropertyInfo(Variant::INT, "id"), PropertyInfo(Variant::STRING, "kind"), PropertyInfo(Variant::DICTIONARY, "request")));
        ADD_SIGNAL(MethodInfo("snapshot", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::DICTIONARY, "result")));
        ADD_SIGNAL(MethodInfo("resource_packs_changed"));
        ADD_SIGNAL(MethodInfo("sprite_ready", PropertyInfo(Variant::INT, "id"), PropertyInfo(Variant::DICTIONARY, "sprite")));
    }

    void AntaresGame::set_game_dir(const String& dir) {
        game_dir_ = dir;
    }
    String AntaresGame::get_game_dir() const {
        return game_dir_;
    }
    void AntaresGame::set_args(const PackedStringArray& args) {
        args_ = args;
    }
    PackedStringArray AntaresGame::get_args() const {
        return args_;
    }
    void AntaresGame::set_autostart(bool value) {
        autostart_ = value;
    }
    bool AntaresGame::get_autostart() const {
        return autostart_;
    }

    void AntaresGame::_ready() {
        set_focus_mode(FOCUS_ALL);
        if (autostart_ && !Engine::get_singleton()->is_editor_hint()) {
            start();
        }
    }

    bool AntaresGame::start() {
        if (started_) {
            return true;
        }
        String dir = game_dir_;
        if (dir.is_empty()) {
            dir = OS::get_singleton()->get_environment("ANTARES_GAME_DIR");
        }
        // An exported build lives in the game folder, beside antares.
        if (dir.is_empty() && OS::get_singleton()->has_feature("template")) {
            dir = OS::get_singleton()->get_executable_path().get_base_dir();
        }
        if (dir.begins_with("res://") || dir.begins_with("user://")) {
            dir = ProjectSettings::get_singleton()->globalize_path(dir);
        }
        if (dir.is_empty()) {
            UtilityFunctions::push_error("AntaresGame: set game_dir or ANTARES_GAME_DIR to a folder with the game data");
            return false;
        }
        // The game's options replace the scene's. They are given exactly as to antares
        // (/config x on Windows, -config x elsewhere; Godot ignores options it doesn't know but
        // reports them), or after "--". The trial's --smoke, --shots and --no-game-cursor are not
        // game options.
        PackedStringArray game_args = args_;
        PackedStringArray found;
        const String prefix = String::chr(command_line::OPTION_PREFIX);
        const PackedStringArray engine_args = OS::get_singleton()->get_cmdline_args();
        const PackedStringArray user_args = OS::get_singleton()->get_cmdline_user_args();
        PackedStringArray all = engine_args;
        all.append_array(user_args);
        for (int64_t i = 0; i < all.size(); ++i) {
            const String& arg = all[i];
            if (arg == prefix + String("skip-intro")) {
                found.push_back(arg);
            } else if ((arg == prefix + String("config") || arg == prefix + String("load-save")) && i + 1 < all.size()) {
                found.push_back(arg);
                found.push_back(all[++i]);
            } else if (arg.begins_with(prefix + String("config=")) || arg.begins_with(prefix + String("load-save="))) {
                found.push_back(arg);
            }
        }
        if (!found.is_empty()) {
            game_args = found;
        }
        std::vector<std::string> args = {"antares"};
        for (int64_t i = 0; i < game_args.size(); ++i) {
            args.push_back(game_args[i].utf8().get_data());
        }
        const std::string game_dir = dir.utf8().get_data();

        antares::host::Activate();
        // The game's platform loop keeps its SDL window, but nobody sees it.
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        started_ = true;
        game_finished_ = false;
        thread_ = std::thread([this, game_dir, args]() {
            if (chdir(game_dir.c_str()) != 0) {
                game_finished_ = true;
                return;
            }
            std::vector<char*> argv;
            for (const std::string& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            antares_hosted_main((int)args.size(), argv.data());
            game_finished_ = true;
        });
        set_process(true);
        grab_focus();
        return true;
    }

    void AntaresGame::stop() {
        if (!started_) {
            return;
        }
        antares::host::Request_Quit();
        // The game thread notices the request at its next input poll; don't hang Godot's exit if
        // it doesn't.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!game_finished_ && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (thread_.joinable()) {
            if (!game_finished_) {
                thread_.detach();
            } else {
                thread_.join();
            }
        }
        started_ = false;
    }

    bool AntaresGame::is_running() const {
        return started_ && !game_finished_;
    }

    void AntaresGame::_process(double) {
        if (!started_) {
            return;
        }
        // The game quit (its own menu, Alt+Q, a fatal error): close the frontend with it.
        if (game_finished_ && !exit_signalled_) {
            exit_signalled_ = true;
            if (thread_.joinable()) {
                thread_.join();
            }
            emit_signal("game_exited");
            return;
        }
        if (!window_applied_ && antares::host::Game_Running()) {
            window_applied_ = true;
            Apply_Window_Settings();
        }
        // --no-game-cursor: keep the system pointer (diagnosis).
        static const bool game_cursor = !OS::get_singleton()->get_cmdline_user_args().has("--no-game-cursor");
        if (game_cursor) {
            Apply_Cursor(false);
        }
        int32_t row_first = 0;
        int32_t row_last = 0;
        if (antares::host::Take_Frame(&frame_, &width_, &height_, &frame_generation_, &row_first, &row_last)) {
            ++frames_taken_;
            Upload_Rows(row_first, row_last);
            queue_redraw();
        }
        const int64_t galaxy = (int64_t)antares::host::Galaxy().generation;
        if (galaxy != galaxy_generation_) {
            galaxy_generation_ = galaxy;
            emit_signal("galaxy_changed");
        }
        Poll_Native();
    }

    void AntaresGame::Poll_Native() {
        uint64_t packs = 0;
        (void)antares::native::Resource_Packs(&packs);
        if (packs != packs_generation_) {
            packs_generation_ = packs;
            emit_signal("resource_packs_changed");
        }
        // Read the visit first: a screen entered after it is caught on the next frame.
        const uint64_t visit = antares::native::Native_Visit();
        const int64_t screen = antares::native::Native_Screen();
        bool hold = false;
        if (screen < 0 && native_screen_ >= 0) {
            // Native to legacy: the frame shown last is still the screen from before the native view
            // took over (owner, 2026-11-06: Colonies and Planets flashed the old galaxy, longest the
            // first time while the screen's art loads). Keep the native view, without input, until the
            // legacy screen presents.
            constexpr int64_t kLegacyWaitMs = 10000;
            const int64_t now = (int64_t)Time::get_singleton()->get_ticks_msec();
            if (legacy_wait_start_ms_ < 0) {
                legacy_wait_start_ms_ = now;
                get_viewport()->set_disable_input(true);
            }
            // A request (a message box) means the legacy side waits for an answer: stop holding.
            hold = frame_generation_ <= antares::native::Leave_Frame_Generation() && now - legacy_wait_start_ms_ < kLegacyWaitMs && !antares::native::Request_Pending();
        }
        if (!hold && legacy_wait_start_ms_ >= 0) {
            legacy_wait_start_ms_ = -1;
            get_viewport()->set_disable_input(false);
        }
        if (!hold && (screen != native_screen_ || (screen >= 0 && visit != native_visit_))) {
            native_screen_ = screen;
            native_visit_ = visit;
            emit_signal("native_screen_changed", screen);
        }
        std::string json;
        while (antares::native::Take_Result(&json)) {
            const Variant parsed = JSON::parse_string(String::utf8(json.c_str()));
            const Dictionary result = parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
            emit_signal("command_result", result.get("id", 0), result);
        }
        antares::native::s_request request;
        while (antares::native::Take_Request(&request)) {
            const Variant parsed = JSON::parse_string(String::utf8(request.json.c_str()));
            const Dictionary fields = parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
            emit_signal("request", (int64_t)request.id, String(request.kind.c_str()), fields);
        }
        while (antares::native::Take_Snapshot(&json)) {
            const Variant parsed = JSON::parse_string(String::utf8(json.c_str()));
            const Dictionary snapshot = parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
            emit_signal("snapshot", snapshot.get("snapshot", ""), snapshot.get("result", Dictionary()));
        }
        uint64_t sprite_id = 0;
        antares::sprites::s_sprite sprite;
        while (antares::native::Take_Sprite(&sprite_id, &sprite)) {
            Dictionary out;
            out["lbx"] = String(sprite.lbx.c_str());
            out["entry"] = sprite.entry;
            out["width"] = sprite.width;
            out["height"] = sprite.height;
            out["scale"] = sprite.scale;
            out["hd"] = sprite.hd;
            out["error"] = String(sprite.error.c_str());
            Array frames;
            for (antares::sprites::s_frame& frame : sprite.frames) {
                PackedByteArray bytes;
                bytes.resize((int64_t)frame.rgba.size());
                if (!frame.rgba.empty()) {
                    memcpy(bytes.ptrw(), frame.rgba.data(), frame.rgba.size());
                }
                Dictionary item;
                item["image"] = Image::create_from_data(frame.width, frame.height, false, Image::FORMAT_RGBA8, bytes);
                item["origin"] = Vector2i(frame.origin_x, frame.origin_y);
                frames.push_back(item);
            }
            out["frames"] = frames;
            emit_signal("sprite_ready", (int64_t)sprite_id, out);
        }
    }

    int64_t AntaresGame::request_sprite(const String& lbx, int64_t entry) {
        const CharString name = lbx.utf8();
        return (int64_t)antares::native::Request_Sprite(std::string(name.get_data(), (size_t)name.length()), (int32_t)entry);
    }

    void AntaresGame::claim_screen(int64_t screen, bool claimed) {
        antares::native::Claim_Screen((int16_t)screen, claimed);
    }

    void AntaresGame::claim_requests(const PackedStringArray& kinds) {
        std::vector<std::string> list;
        for (int64_t i = 0; i < kinds.size(); ++i) {
            const CharString kind = kinds[i].utf8();
            list.emplace_back(kind.get_data(), (size_t)kind.length());
        }
        antares::native::Claim_Requests(list);
    }

    int64_t AntaresGame::send_command(const Dictionary& command) {
        const CharString json = JSON::stringify(command).utf8();
        return (int64_t)antares::native::Send_Command(std::string(json.get_data(), (size_t)json.length()));
    }

    int64_t AntaresGame::get_native_screen() const {
        return native_screen_;
    }

    int64_t AntaresGame::get_native_visit() const {
        return (int64_t)native_visit_;
    }

    void AntaresGame::answer_request(int64_t id, int64_t value) {
        antares::native::Answer_Request((uint64_t)id, (int32_t)value);
    }

    Ref<Image> AntaresGame::get_frame_image() const {
        if (width_ <= 0 || height_ <= 0 || frame_.size() < (size_t)width_ * (size_t)height_) {
            return Ref<Image>();
        }
        PackedByteArray bytes;
        bytes.resize((int64_t)width_ * height_ * 4);
        uint8_t* out = bytes.ptrw();
        for (size_t i = 0; i < (size_t)width_ * (size_t)height_; ++i) {
            const uint32_t argb = frame_[i];
            out[i * 4 + 0] = (uint8_t)(argb >> 16);
            out[i * 4 + 1] = (uint8_t)(argb >> 8);
            out[i * 4 + 2] = (uint8_t)argb;
            out[i * 4 + 3] = 255;
        }
        return Image::create_from_data(width_, height_, false, Image::FORMAT_RGBA8, bytes);
    }

    PackedStringArray AntaresGame::get_resource_packs() const {
        PackedStringArray out;
        for (const std::string& path : antares::native::Resource_Packs(nullptr)) {
            out.push_back(String::utf8(path.c_str()));
        }
        return out;
    }

    void AntaresGame::Upload_Rows(int32_t row_first, int32_t row_last) {
        if (width_ <= 0 || height_ <= 0) {
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        const int32_t strip_count = (height_ + k_strip_rows - 1) / k_strip_rows;
        const bool first = (int32_t)strips_[0].size() != strip_count;
        if (first) {
            // A 640x480 frame (no HD pack) is enlarged pixel for pixel; the 4x frame is filtered.
            set_texture_filter(width_ <= 640 ? TEXTURE_FILTER_NEAREST : TEXTURE_FILTER_LINEAR);
            strips_[0].assign((size_t)strip_count, Ref<ImageTexture>());
            strips_[1].assign((size_t)strip_count, Ref<ImageTexture>());
            strip_current_.assign((size_t)strip_count, 1);
            row_first = 0;
            row_last = height_;
        }
        for (int32_t strip = row_first / k_strip_rows; strip < strip_count && strip * k_strip_rows < row_last; ++strip) {
            const int32_t y0 = strip * k_strip_rows;
            const int32_t rows = std::min(k_strip_rows, height_ - y0);
            const int64_t bytes = (int64_t)width_ * rows * 4;
            if (strip_bytes_.size() != bytes) {
                strip_bytes_.resize(bytes);
            }
            // ARGB8888 words are B, G, R, A in memory; the material swaps red and blue.
            memcpy(strip_bytes_.ptrw(), frame_.data() + (size_t)y0 * (size_t)width_, (size_t)bytes);
            Ref<Image> image = Image::create_from_data(width_, rows, false, Image::FORMAT_RGBA8, strip_bytes_);
            const uint8_t next = (uint8_t)(1 - strip_current_[(size_t)strip]);
            Ref<ImageTexture>& texture = strips_[next][(size_t)strip];
            if (texture.is_null()) {
                texture = ImageTexture::create_from_image(image);
            } else {
                texture->update(image);
            }
            strip_current_[(size_t)strip] = next;
            ++strips_uploaded_;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        upload_ms_total_ += ms;
        upload_ms_max_ = std::max(upload_ms_max_, ms);
        if (!first && row_first == 0 && row_last == height_) {
            ++full_uploads_;
            full_upload_ms_total_ += ms;
        }
    }

    Rect2 AntaresGame::Frame_Rect() const {
        const Vector2 size = get_size();
        const float scale = std::min(size.x / 640.0f, size.y / 480.0f);
        const Vector2 frame(640.0f * scale, 480.0f * scale);
        return Rect2((size - frame) * 0.5f, frame);
    }

    bool AntaresGame::To_Game(const Vector2& local, Vector2* game) const {
        const Rect2 rect = Frame_Rect();
        if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) {
            return false;
        }
        *game = (local - rect.position) / rect.size * Vector2(640.0f, 480.0f);
        game->x = CLAMP(game->x, 0.0f, 639.0f);
        game->y = CLAMP(game->y, 0.0f, 479.0f);
        return true;
    }

    void AntaresGame::_draw() {
        if (strip_current_.empty() || height_ <= 0) {
            return;
        }
        const Rect2 rect = Frame_Rect();
        for (size_t i = 0; i < strip_current_.size(); ++i) {
            const Ref<ImageTexture>& strip = strips_[strip_current_[i]][i];
            if (strip.is_null()) {
                continue;
            }
            const int32_t y0 = (int32_t)i * k_strip_rows;
            const int32_t rows = std::min(k_strip_rows, height_ - y0);
            const Rect2 area(rect.position.x, rect.position.y + rect.size.y * (float)y0 / (float)height_, rect.size.x, rect.size.y * (float)rows / (float)height_);
            draw_texture_rect(strip, area, false);
        }
    }

    void AntaresGame::_gui_input(const Ref<InputEvent>& event) {
        if (!started_) {
            return;
        }
        Ref<InputEventMouseMotion> motion = event;
        if (motion.is_valid()) {
            Vector2 game;
            if (To_Game(motion->get_position(), &game)) {
                antares::host::Send_Mouse_Motion(game.x, game.y);
            }
            accept_event();
            return;
        }
        Ref<InputEventMouseButton> button = event;
        if (button.is_valid()) {
            Vector2 game;
            if (!To_Game(button->get_position(), &game)) {
                return;
            }
            switch (button->get_button_index()) {
                case MOUSE_BUTTON_LEFT: antares::host::Send_Mouse_Button(game.x, game.y, 1, button->is_pressed()); break;
                case MOUSE_BUTTON_RIGHT: antares::host::Send_Mouse_Button(game.x, game.y, 2, button->is_pressed()); break;
                case MOUSE_BUTTON_MIDDLE: antares::host::Send_Mouse_Button(game.x, game.y, 3, button->is_pressed()); break;
                case MOUSE_BUTTON_WHEEL_UP:
                    if (button->is_pressed()) {
                        antares::host::Send_Wheel(game.x, game.y, 1.0f);
                    }
                    break;
                case MOUSE_BUTTON_WHEEL_DOWN:
                    if (button->is_pressed()) {
                        antares::host::Send_Wheel(game.x, game.y, -1.0f);
                    }
                    break;
                default: break;
            }
            grab_focus();
            accept_event();
            return;
        }
        Ref<InputEventKey> key = event;
        if (key.is_valid() && key->is_pressed() && !key->is_echo() && key->is_alt_pressed() &&
            (key->get_keycode() == KEY_ENTER || key->get_keycode() == KEY_KP_ENTER)) {
            // Alt+Enter switches fullscreen, as in the SDL build.
            DisplayServer* display = DisplayServer::get_singleton();
            const bool full = display->window_get_mode() == DisplayServer::WINDOW_MODE_FULLSCREEN;
            display->window_set_mode(full ? DisplayServer::WINDOW_MODE_WINDOWED : DisplayServer::WINDOW_MODE_FULLSCREEN);
            accept_event();
            return;
        }
        if (key.is_valid() && !key->is_echo()) {
            const uint32_t sdl_key = To_Sdl_Key(key->get_keycode());
            if (sdl_key != 0) {
                antares::host::Send_Key(sdl_key,
                    To_Sdl_Mod(key->is_shift_pressed(), key->is_alt_pressed(), key->is_ctrl_pressed()),
                    key->is_pressed());
                accept_event();
            }
        }
    }

    void AntaresGame::Apply_Window_Settings() {
        if (Engine::get_singleton()->is_editor_hint() || OS::get_singleton()->get_cmdline_user_args().has("--smoke")) {
            return;
        }
        int32_t width = 0;
        int32_t height = 0;
        bool fullscreen = false;
        antares::host::Window_Settings(&width, &height, &fullscreen);
        DisplayServer* display = DisplayServer::get_singleton();
        display->window_set_title("Battle at Antares");
        if (fullscreen) {
            display->window_set_mode(DisplayServer::WINDOW_MODE_FULLSCREEN);
        } else if (width > 0 && height > 0) {
            const Vector2i size(width, height);
            const Rect2i screen = display->screen_get_usable_rect(display->window_get_current_screen());
            display->window_set_size(size.min(screen.size));
            display->window_set_position(screen.position + (screen.size - size.min(screen.size)) / 2);
        }
    }

    void AntaresGame::Apply_Cursor(bool force) {
        Input* input = Input::get_singleton();
        const bool changed = antares::host::Take_Cursor(&cursor_argb_, &cursor_size_, &cursor_generation_);
        const float scale = Frame_Rect().size.x / 640.0f;
        if (!is_visible_in_tree()) {
            if (cursor_custom_) {
                input->set_custom_mouse_cursor(Ref<Resource>());
                cursor_custom_ = false;
            }
            return;
        }
        if (!changed && !force && std::fabs(scale - cursor_scale_) < 0.01f) {
            return;
        }
        cursor_scale_ = scale;
        // Hidden: a transparent pixel; otherwise the game's image at the frame's scale
        // (24 game pixels wide), its top-left corner at the pointer.
        PackedByteArray bytes;
        int32_t size = cursor_size_ > 0 ? cursor_size_ : 1;
        bytes.resize((int64_t)size * size * 4);
        uint8_t* out = bytes.ptrw();
        for (int64_t i = 0; i < (int64_t)size * size; ++i) {
            const uint32_t px = cursor_size_ > 0 ? cursor_argb_[(size_t)i] : 0u;
            out[i * 4 + 0] = (uint8_t)(px >> 16);
            out[i * 4 + 1] = (uint8_t)(px >> 8);
            out[i * 4 + 2] = (uint8_t)px;
            out[i * 4 + 3] = (uint8_t)(px >> 24);
        }
        Ref<Image> image = Image::create_from_data(size, size, false, Image::FORMAT_RGBA8, bytes);
        if (cursor_size_ > 0) {
            const int32_t shown = std::clamp((int32_t)std::lround(24.0f * scale), 8, 256);
            image->resize(shown, shown, cursor_size_ > 24 ? Image::INTERPOLATE_LANCZOS : Image::INTERPOLATE_NEAREST);
        }
        input->set_custom_mouse_cursor(image, Input::CURSOR_ARROW, Vector2());
        cursor_custom_ = true;
    }

    void AntaresGame::_notification(int what) {
        if (what == NOTIFICATION_VISIBILITY_CHANGED || what == NOTIFICATION_RESIZED) {
            Apply_Cursor(true);
        }
    }

    void AntaresGame::request_game_quit() {
        antares::host::Request_Quit();
    }

    void AntaresGame::send_key(int64_t godot_keycode, bool shift, bool alt, bool ctrl) {
        const uint32_t sdl_key = To_Sdl_Key((Key)godot_keycode);
        if (sdl_key == 0) {
            return;
        }
        antares::host::Send_Key(sdl_key, To_Sdl_Mod(shift, alt, ctrl), true);
        antares::host::Send_Key(sdl_key, To_Sdl_Mod(shift, alt, ctrl), false);
    }

    TypedArray<Dictionary> AntaresGame::get_stars() const {
        TypedArray<Dictionary> out;
        const antares::host::s_galaxy galaxy = antares::host::Galaxy();
        if (!galaxy.valid) {
            return out;
        }
        int64_t index = 0;
        for (const antares::host::s_star& star : galaxy.stars) {
            Dictionary d;
            d["index"] = index++;
            d["name"] = String::utf8(star.name.c_str());
            d["x"] = star.x;
            d["y"] = star.y;
            d["size"] = star.size;
            d["spectral_class"] = star.spectral_class;
            d["owner"] = star.owner;
            d["owner_color"] = star.owner_color;
            d["owner_name"] = String::utf8(star.owner_name.c_str());
            d["in_nebula"] = star.in_nebula;
            d["special"] = star.special;
            out.push_back(d);
        }
        return out;
    }

    int64_t AntaresGame::get_galaxy_generation() const {
        return galaxy_generation_;
    }

    Dictionary AntaresGame::get_stats() const {
        Dictionary d;
        d["frames"] = (int64_t)frames_taken_;
        d["strips_uploaded"] = (int64_t)strips_uploaded_;
        d["upload_ms_avg"] = frames_taken_ > 0 ? upload_ms_total_ / (double)frames_taken_ : 0.0;
        d["upload_ms_max"] = upload_ms_max_;
        d["full_uploads"] = (int64_t)full_uploads_;
        d["full_upload_ms_avg"] = full_uploads_ > 0 ? full_upload_ms_total_ / (double)full_uploads_ : 0.0;
        d["cursor_size"] = cursor_size_;
        d["width"] = width_;
        d["height"] = height_;
        return d;
    }

} // namespace godot

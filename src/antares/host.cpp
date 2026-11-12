#include "pch.h"

#include "antares/host.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace antares::host {
    namespace {
        std::atomic<bool> s_active{false};

        // The newest resolved frame and the rows changed since the host last took it.
        std::mutex s_frame_mutex;
        std::vector<uint32_t> s_frame;
        int32_t s_frame_width = 0;
        int32_t s_frame_height = 0;
        uint64_t s_frame_generation = 0;
        int32_t s_dirty_first = 0;
        int32_t s_dirty_last = 0;

        std::mutex s_cursor_mutex;
        std::vector<uint32_t> s_cursor;
        int32_t s_cursor_size = -1;
        uint64_t s_cursor_generation = 0;

        std::mutex s_galaxy_mutex;
        s_galaxy s_published;

        // Host input arrives here from the host's thread; SDL_PushEvent is thread safe and the
        // platform loop reads the events as if they came from a window.
        SDL_Window* Window() {
            return static_cast<SDL_Window*>(platform::g_main_window);
        }

        // Game coordinates to the dummy window's coordinates, matching the renderer's 640x480
        // letterbox presentation (Ensure_Render_Presentation).
        void To_Window(float game_x, float game_y, float* window_x, float* window_y) {
            int w = video::g_screen_res_x;
            int h = video::g_screen_res_y;
            if (Window() != nullptr) {
                (void)SDL_GetWindowSize(Window(), &w, &h);
            }
            const float logical_w = (float)std::max<int32_t>(1, video::g_screen_res_x);
            const float logical_h = (float)std::max<int32_t>(1, video::g_screen_res_y);
            const float scale = std::min((float)w / logical_w, (float)h / logical_h);
            *window_x = ((float)w - logical_w * scale) * 0.5f + game_x * scale;
            *window_y = ((float)h - logical_h * scale) * 0.5f + game_y * scale;
        }

        void Push(SDL_Event* event) {
            if (Window() == nullptr) {
                return;
            }
            (void)SDL_PushEvent(event);
        }

        uint32_t s_buttons = 0; // SDL_MouseButtonFlags the host holds down
    } // namespace

    void Activate() {
        s_active = true;
    }

    bool Active() {
        return s_active;
    }

    void Request_Quit() {
        platform::Request_Exit();
    }

    bool Game_Running() {
        return Window() != nullptr && !platform::Game_Thread_Exited();
    }

    bool Native_UI() {
        return platform::g_native_ui != 0;
    }

    void Window_Settings(int32_t* width, int32_t* height, bool* fullscreen) {
        *width = platform::g_window_width;
        *height = platform::g_window_height;
        *fullscreen = platform::g_fullscreen != 0;
    }

    uint64_t Frame_Generation() {
        std::lock_guard<std::mutex> lock(s_frame_mutex);
        return s_frame_generation;
    }

    bool Take_Frame(std::vector<uint32_t>* argb, int32_t* width, int32_t* height, uint64_t* generation,
        int32_t* row_first, int32_t* row_last) {
        std::lock_guard<std::mutex> lock(s_frame_mutex);
        if (s_frame_generation == *generation || s_frame.empty()) {
            return false;
        }
        const bool resized = argb->size() != s_frame.size();
        if (resized) {
            argb->assign(s_frame.size(), 0u);
            s_dirty_first = 0;
            s_dirty_last = s_frame_height;
        }
        const size_t row = (size_t)s_frame_width;
        std::copy(s_frame.begin() + (ptrdiff_t)(row * (size_t)s_dirty_first),
            s_frame.begin() + (ptrdiff_t)(row * (size_t)s_dirty_last),
            argb->begin() + (ptrdiff_t)(row * (size_t)s_dirty_first));
        *width = s_frame_width;
        *height = s_frame_height;
        *generation = s_frame_generation;
        *row_first = s_dirty_first;
        *row_last = s_dirty_last;
        s_dirty_first = s_frame_height;
        s_dirty_last = 0;
        return true;
    }

    bool Take_Cursor(std::vector<uint32_t>* argb, int32_t* size, uint64_t* generation) {
        std::lock_guard<std::mutex> lock(s_cursor_mutex);
        if (s_cursor_generation == *generation) {
            return false;
        }
        *argb = s_cursor;
        *size = s_cursor_size;
        *generation = s_cursor_generation;
        return true;
    }

    void Cursor_Image(const uint32_t* argb, int32_t size) {
        if (!s_active) {
            return;
        }
        const size_t count = (size_t)size * (size_t)size;
        std::lock_guard<std::mutex> lock(s_cursor_mutex);
        if (size == s_cursor_size && std::equal(argb, argb + count, s_cursor.begin())) {
            return;
        }
        s_cursor.assign(argb, argb + count);
        s_cursor_size = size;
        ++s_cursor_generation;
    }

    void Send_Mouse_Motion(float game_x, float game_y) {
        SDL_Event event = {};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(Window());
        event.motion.state = s_buttons;
        To_Window(game_x, game_y, &event.motion.x, &event.motion.y);
        Push(&event);
    }

    void Send_Mouse_Button(float game_x, float game_y, int32_t button, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = SDL_GetWindowID(Window());
        event.button.button = (Uint8)(button == 1 ? SDL_BUTTON_LEFT : button == 2 ? SDL_BUTTON_RIGHT : SDL_BUTTON_MIDDLE);
        event.button.down = down;
        event.button.clicks = 1;
        const uint32_t mask = SDL_BUTTON_MASK(event.button.button);
        s_buttons = down ? (s_buttons | mask) : (s_buttons & ~mask);
        To_Window(game_x, game_y, &event.button.x, &event.button.y);
        Push(&event);
    }

    void Send_Wheel(float game_x, float game_y, float steps) {
        SDL_Event event = {};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = SDL_GetWindowID(Window());
        event.wheel.y = steps;
        event.wheel.integer_y = (int)steps;
        event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        To_Window(game_x, game_y, &event.wheel.mouse_x, &event.wheel.mouse_y);
        Push(&event);
    }

    void Send_Key(uint32_t keycode, uint16_t modifiers, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(Window());
        event.key.key = (SDL_Keycode)keycode;
        event.key.mod = (SDL_Keymod)modifiers;
        event.key.down = down;
        Push(&event);
    }

    s_galaxy Galaxy() {
        std::lock_guard<std::mutex> lock(s_galaxy_mutex);
        return s_published;
    }

    bool Frame_Resolved(const uint32_t* argb, int32_t width, int32_t height, bool changed, int32_t row_first,
        int32_t row_last) {
        if (!s_active) {
            return false;
        }
        std::lock_guard<std::mutex> lock(s_frame_mutex);
        const size_t size = (size_t)width * (size_t)height;
        if (s_frame.size() != size) {
            s_frame.assign(size, 0u);
            s_frame_width = width;
            s_frame_height = height;
            changed = true;
            row_first = 0;
            row_last = height;
        }
        if (changed && row_last > row_first) {
            std::copy(argb + (size_t)row_first * (size_t)width, argb + (size_t)row_last * (size_t)width,
                s_frame.begin() + (ptrdiff_t)((size_t)row_first * (size_t)width));
            s_dirty_first = std::min(s_dirty_first, row_first);
            s_dirty_last = std::max(s_dirty_last, row_last);
            ++s_frame_generation;
        }
        return true;
    }

    void On_Publish() {
        if (!s_active) {
            return;
        }
        s_galaxy next;
        next.valid = MOX::star != nullptr && MOX::NUM_STARS > 0 && MOX::NUM_STARS <= MAX_STARS;
        if (next.valid) {
            next.stars.reserve((size_t)MOX::NUM_STARS);
            for (int16_t i = 0; i < MOX::NUM_STARS; ++i) {
                const s_star_data& data = MOX::star[i];
                s_star star;
                star.name.assign(data.name, strnlen(data.name, sizeof(data.name)));
                star.x = data.x;
                star.y = data.y;
                star.size = data.size;
                star.spectral_class = data.spectral_class;
                star.owner = data.owner;
                star.in_nebula = data.in_nebula != 0;
                star.special = data.system_special;
                if (data.owner >= 0 && data.owner < MAX_PLAYERS && MOX::player != nullptr && data.owner < MOX::NUM_PLAYERS) {
                    const s_player& owner = MOX::player[data.owner];
                    star.owner_color = owner.color;
                    star.owner_name.assign(owner.race_name, strnlen(owner.race_name, sizeof(owner.race_name)));
                }
                next.stars.push_back(std::move(star));
            }
        }
        std::lock_guard<std::mutex> lock(s_galaxy_mutex);
        const auto same_star = [](const s_star& a, const s_star& b) {
            return a.name == b.name && a.x == b.x && a.y == b.y && a.owner == b.owner && a.size == b.size &&
                a.spectral_class == b.spectral_class && a.owner_name == b.owner_name;
        };
        const bool same = next.valid == s_published.valid && next.stars.size() == s_published.stars.size() &&
            std::equal(next.stars.begin(), next.stars.end(), s_published.stars.begin(), same_star);
        if (!same) {
            next.generation = s_published.generation + 1;
            s_published = std::move(next);
        }
    }

} // namespace antares::host

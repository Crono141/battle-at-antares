#include "pch.h"

#include "antares/control.h"
#include "antares/host.h"
#include "antares/identity.h"
#include "antares/input_trace.h"
#include "antares/lbx_trace.h"
#include "antares/script_capture.h"
#include "antares/ui_hooks.h"

#include "config/config_runtime.h"
#include "game/scalers/scaler.h"

int16_t g_cached_mouse_x;
int16_t g_cached_mouse_y;
int32_t g_window_focus_state;
uint32_t g_last_scan_code;

namespace platform {
    namespace {
        bool g_has_lost_focus;
        SDL_Window* g_sdl_window;
        SDL_Renderer* g_sdl_renderer;
        SDL_Texture* g_sdl_present_texture;
        SDL_Surface* g_sdl_screen_index_surface;
        SDL_Surface* g_sdl_present_argb_surface;
        SDL_Surface* g_sdl_cursor_surface;
        SDL_Palette* g_sdl_present_palette;
        uint32_t* g_sdl_present_pixels;
        scalers::s_pipeline g_present_scaler_pipeline;
        video::s_palette_color g_present_palette_entries[256];
        constexpr int32_t k_cursor_dimension = 24;
        constexpr int32_t k_cursor_pixel_count = k_cursor_dimension * k_cursor_dimension;
        constexpr uint16_t k_input_queue_capacity = 256;

        enum e_input_event_type : uint8_t {
            INPUT_EVENT_MOUSE = 0,
            INPUT_EVENT_KEY = 1,
        };

        struct s_input_event {
            e_input_event_type type;
            int16_t mouse_x;
            int16_t mouse_y;
            uint16_t mouse_buttons;
            uint32_t key_code;
            uint32_t scan_code;
        };

        struct s_present_cursor_state {
            uint8_t* pixels;
            int16_t offset;
            int16_t shape;
            uint8_t enabled;
        };

        SDL_Mutex* g_input_queue_mutex;
        SDL_Mutex* g_present_cursor_mutex;
        s_input_event g_input_event_queue[k_input_queue_capacity];
        uint16_t g_input_queue_read;
        uint16_t g_input_queue_write;
        uint16_t g_sdl_mouse_buttons;
        uint16_t g_game_mouse_buttons;
        bool g_game_mouse_suppressed;
        s_present_cursor_state g_pending_cursor_state;
        s_present_cursor_state g_active_cursor_state;
        uint32_t g_pending_cursor_generation;
        uint32_t g_applied_cursor_generation;
        SDL_AtomicInt g_present_mouse_x;
        SDL_AtomicInt g_present_mouse_y;
        SDL_AtomicInt g_present_refresh_requested;
        SDL_Mutex* g_exit_mutex;
        SDL_Condition* g_exit_condition;
        SDL_AtomicInt g_exit_requested;
        SDL_AtomicInt g_exit_status;
        SDL_AtomicInt g_game_thread_exited;
        SDL_AtomicInt g_requested_present_scaler;
        SDL_AtomicInt g_requested_vsync_enabled;
        e_present_scaler g_applied_present_scaler = (e_present_scaler)UINT8_MAX;
        int8_t g_applied_vsync_enabled = -1;
        uint32_t g_present_generation;

        void Present_Indexed_Buffer(const uint8_t* src_pixels);
        bool Ensure_Present_Palette();
        bool Ensure_Cursor_Surface();
        bool Ensure_Render_Presentation();
        bool Ensure_Scaler_Output();
        void Destroy_Scaler_Output();
        void Present_Texture_To_Output(SDL_Texture* texture, bool draw_cursor = false);
        void Save_Pending_Screenshot();
        void Render_Cursor_Overlay();
        void Apply_Pending_Cursor_State();
        void Blit_Cursor_On_Present_Surface();
        void Sync_Mouse_State_From_SDL();
        void Enqueue_Mouse_Input_Event(int16_t mouse_x, int16_t mouse_y, uint16_t mouse_buttons);
        void Request_Present_Refresh();

        e_present_scaler Requested_Present_Scaler() {
            return (e_present_scaler)SDL_GetAtomicInt(&g_requested_present_scaler);
        }

        void Publish_Present_Scaler_Config() {
            SDL_SetAtomicInt(&g_requested_present_scaler, (int)platform::g_present_scaler);
            SDL_SetAtomicInt(&g_requested_vsync_enabled, platform::g_vsync_enabled != 0 ? 1 : 0);
            Request_Present_Refresh();
        }

        void Sync_Debug_Overlay_Cursor() {
            const bool cursor_visible = debug_overlay::Visible();
            if (SDL_CursorVisible() == cursor_visible) {
                return;
            }

            if (cursor_visible) {
                (void)SDL_ShowCursor();
            } else {
                (void)SDL_HideCursor();
            }
        }

        void Forward_Game_Mouse_State(int16_t x, int16_t y, uint16_t buttons, bool overlay_captures_mouse) {
            if (overlay_captures_mouse) {
                g_game_mouse_suppressed = true;
                if (g_game_mouse_buttons != 0) {
                    g_game_mouse_buttons = 0;
                    Enqueue_Mouse_Input_Event(x, y, 0);
                }
                return;
            }
            if (g_game_mouse_suppressed) {
                if (buttons != 0) {
                    return;
                }
                g_game_mouse_suppressed = false;
            }

            g_game_mouse_buttons = buttons;
            Enqueue_Mouse_Input_Event(x, y, buttons);
        }

        void Request_Present_Refresh() {
            SDL_SetAtomicInt(&g_present_refresh_requested, 1);
        }

        bool Consume_Present_Refresh_Request() {
            return SDL_SetAtomicInt(&g_present_refresh_requested, 0) != 0;
        }

        void Set_Present_Mouse_Position(int16_t x, int16_t y) {
            const int previous_x = SDL_GetAtomicInt(&g_present_mouse_x);
            const int previous_y = SDL_GetAtomicInt(&g_present_mouse_y);

            SDL_SetAtomicInt(&g_present_mouse_x, (int)x);
            SDL_SetAtomicInt(&g_present_mouse_y, (int)y);

            if (previous_x != (int)x || previous_y != (int)y) {
                Request_Present_Refresh();
            }
        }

        uint16_t Previous_Input_Queue_Index(uint16_t index) {
            return (index == 0) ? (uint16_t)(k_input_queue_capacity - 1) : (uint16_t)(index - 1);
        }

        void Enqueue_Key_Input_Event(uint32_t key_code, uint32_t scan_code) {
            if (g_input_queue_mutex == nullptr) {
                return;
            }

            SDL_LockMutex(g_input_queue_mutex);

            const uint16_t next_write = (uint16_t)((g_input_queue_write + 1) % k_input_queue_capacity);
            if (next_write == g_input_queue_read) {
                g_input_queue_read = (uint16_t)((g_input_queue_read + 1) % k_input_queue_capacity);
            }

            s_input_event& event = g_input_event_queue[g_input_queue_write];
            event.type = INPUT_EVENT_KEY;
            event.mouse_x = 0;
            event.mouse_y = 0;
            event.mouse_buttons = 0;
            event.key_code = key_code;
            event.scan_code = scan_code;
            g_input_queue_write = next_write;

            SDL_UnlockMutex(g_input_queue_mutex);
        }

        void Enqueue_Mouse_Input_Event(int16_t mouse_x, int16_t mouse_y, uint16_t mouse_buttons) {
            if (g_input_queue_mutex == nullptr) {
                return;
            }

            SDL_LockMutex(g_input_queue_mutex);

            if (g_input_queue_read != g_input_queue_write) {
                const uint16_t last_index = Previous_Input_Queue_Index(g_input_queue_write);
                s_input_event& last_event = g_input_event_queue[last_index];
                if (last_event.type == INPUT_EVENT_MOUSE &&
                    last_event.mouse_buttons == mouse_buttons) {
                    last_event.mouse_x = mouse_x;
                    last_event.mouse_y = mouse_y;
                    SDL_UnlockMutex(g_input_queue_mutex);
                    return;
                }
            }

            const uint16_t next_write = (uint16_t)((g_input_queue_write + 1) % k_input_queue_capacity);
            if (next_write == g_input_queue_read) {
                g_input_queue_read = (uint16_t)((g_input_queue_read + 1) % k_input_queue_capacity);
            }

            s_input_event& event = g_input_event_queue[g_input_queue_write];
            event.type = INPUT_EVENT_MOUSE;
            event.mouse_x = mouse_x;
            event.mouse_y = mouse_y;
            event.mouse_buttons = mouse_buttons;
            event.key_code = 0;
            event.scan_code = 0;
            g_input_queue_write = next_write;

            SDL_UnlockMutex(g_input_queue_mutex);
        }

        int16_t Round_Float_To_Int16(float value) {
            long rounded_value = lroundf(value);
            if (rounded_value < INT16_MIN) {
                return INT16_MIN;
            }
            if (rounded_value > INT16_MAX) {
                return INT16_MAX;
            }

            return (int16_t)rounded_value;
        }

        int16_t Clamp_Game_Coord(float coord, int32_t game_extent) {
            if (game_extent <= 1) {
                return 0;
            }

            long rounded_value = lroundf(coord);
            if (rounded_value < 0) {
                return 0;
            }
            if (rounded_value > game_extent - 1) {
                return (int16_t)(game_extent - 1);
            }

            return (int16_t)rounded_value;
        }

        int16_t Map_Window_Coord_To_Game_Coord(float coord, int32_t window_extent, int32_t game_extent) {
            if (window_extent <= 1 || game_extent <= 1) {
                return 0;
            }

            float scaled = (coord * (float)(game_extent - 1)) / (float)(window_extent - 1);
            long rounded_value = lroundf(scaled);
            if (rounded_value < 0) {
                return 0;
            }
            if (rounded_value > game_extent - 1) {
                return (int16_t)(game_extent - 1);
            }

            return (int16_t)rounded_value;
        }

        float Map_Game_Coord_To_Window_Coord(int16_t coord, int32_t game_extent, int32_t window_extent) {
            if (window_extent <= 1 || game_extent <= 1) {
                return 0.0f;
            }

            return ((float)coord * (float)(window_extent - 1)) / (float)(game_extent - 1);
        }

        void Map_Window_Point_To_Game_Point(
            float window_x,
            float window_y,
            int16_t* game_x,
            int16_t* game_y) {
            if (game_x == nullptr || game_y == nullptr) {
                return;
            }

            float logical_x = window_x;
            float logical_y = window_y;
            if (g_sdl_renderer != nullptr &&
                SDL_RenderCoordinatesFromWindow(g_sdl_renderer, window_x, window_y, &logical_x, &logical_y)) {
                *game_x = Clamp_Game_Coord(logical_x, video::g_screen_res_x);
                *game_y = Clamp_Game_Coord(logical_y, video::g_screen_res_y);
                return;
            }

            int32_t window_width = 0;
            int32_t window_height = 0;
            if (g_sdl_window != nullptr) {
                SDL_GetWindowSize(g_sdl_window, &window_width, &window_height);
            }

            *game_x = Map_Window_Coord_To_Game_Coord(window_x, window_width, video::g_screen_res_x);
            *game_y = Map_Window_Coord_To_Game_Coord(window_y, window_height, video::g_screen_res_y);
        }

        void Get_Raw_Screen_Resolution(int32_t* width, int32_t* height) {
            if (width == nullptr || height == nullptr) {
                return;
            }

            *width = video::g_screen_res_x;
            *height = video::g_screen_res_y;
            if (g_sdl_window != nullptr) {
                (void)SDL_GetWindowSize(g_sdl_window, width, height);
            }
        }

        void Queue_Debug_Overlay_Display_Size() {
            int32_t width = 0;
            int32_t height = 0;
            Get_Raw_Screen_Resolution(&width, &height);
            debug_overlay::Handle_Display_Size(width, height);
        }

        void Map_Game_Point_To_Window_Point(
            int16_t game_x,
            int16_t game_y,
            float* window_x,
            float* window_y) {
            if (window_x == nullptr || window_y == nullptr) {
                return;
            }

            if (g_sdl_renderer != nullptr &&
                SDL_RenderCoordinatesToWindow(g_sdl_renderer, (float)game_x, (float)game_y, window_x, window_y)) {
                return;
            }

            int32_t window_width = 0;
            int32_t window_height = 0;
            if (g_sdl_window != nullptr) {
                SDL_GetWindowSize(g_sdl_window, &window_width, &window_height);
            }

            *window_x = Map_Game_Coord_To_Window_Coord(game_x, video::g_screen_res_x, window_width);
            *window_y = Map_Game_Coord_To_Window_Coord(game_y, video::g_screen_res_y, window_height);
        }

        bool Ensure_Render_Presentation() {
            if (g_sdl_renderer == nullptr || video::g_screen_res_x <= 0 || video::g_screen_res_y <= 0) {
                return false;
            }

            return SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE) &&
                SDL_SetRenderLogicalPresentation(
                    g_sdl_renderer,
                    video::g_screen_res_x,
                    video::g_screen_res_y,
                    SDL_LOGICAL_PRESENTATION_LETTERBOX);
        }

        SDL_Texture* Create_Scaler_Texture(e_present_scaler scaler) {
            const int32_t factor = scalers::Get_Factor(scaler);
            if (factor == 0 || video::g_screen_res_x > INT32_MAX / factor ||
                video::g_screen_res_y > INT32_MAX / factor) {
                return nullptr;
            }

            SDL_Texture* texture = SDL_CreateTexture(
                g_sdl_renderer,
                SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING,
                video::g_screen_res_x * factor,
                video::g_screen_res_y * factor);
            const SDL_ScaleMode scale_mode = scalers::Uses_Linear_Filter(scaler)
                ? SDL_SCALEMODE_LINEAR
                : SDL_SCALEMODE_PIXELART;
            if (texture == nullptr ||
                (!SDL_SetTextureScaleMode(texture, scale_mode) &&
                    scale_mode != SDL_SCALEMODE_NEAREST &&
                    !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))) {
                if (texture != nullptr) {
                    SDL_DestroyTexture(texture);
                }
                return nullptr;
            }
            return texture;
        }

        void Destroy_Scaler_Output() {
            if (g_sdl_present_texture != nullptr) {
                SDL_DestroyTexture(g_sdl_present_texture);
                g_sdl_present_texture = nullptr;
            }
            scalers::Invalidate(&g_present_scaler_pipeline);
        }

        bool Ensure_Scaler_Output() {
            const e_present_scaler requested_scaler = Requested_Present_Scaler();
            if (g_sdl_present_texture != nullptr &&
                g_applied_present_scaler == requested_scaler &&
                scalers::Is_Configured(&g_present_scaler_pipeline, requested_scaler)) {
                return true;
            }

            SDL_Texture* replacement_texture = Create_Scaler_Texture(requested_scaler);
            if (replacement_texture == nullptr ||
                !scalers::Configure(&g_present_scaler_pipeline, requested_scaler)) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "platform: unable to initialize %s presentation: %s",
                    scalers::Get_Name(requested_scaler),
                    SDL_GetError());
                if (replacement_texture != nullptr) {
                    SDL_DestroyTexture(replacement_texture);
                }
                if (g_sdl_present_texture != nullptr) {
                    return true;
                }

                replacement_texture = Create_Scaler_Texture(PRESENT_SCALER_PIXELART);
                if (replacement_texture == nullptr ||
                    !scalers::Configure(
                        &g_present_scaler_pipeline,
                        PRESENT_SCALER_PIXELART)) {
                    if (replacement_texture != nullptr) {
                        SDL_DestroyTexture(replacement_texture);
                    }
                    return false;
                }
                g_sdl_present_texture = replacement_texture;
                g_applied_present_scaler = PRESENT_SCALER_PIXELART;
                return true;
            }

            if (g_sdl_present_texture != nullptr) {
                SDL_DestroyTexture(g_sdl_present_texture);
            }
            g_sdl_present_texture = replacement_texture;
            g_applied_present_scaler = requested_scaler;
            return true;
        }

        void Present_Texture_To_Output(SDL_Texture* texture, bool draw_cursor) {
            if (g_sdl_renderer == nullptr || texture == nullptr) {
                return;
            }

            const int8_t vsync_enabled =
                SDL_GetAtomicInt(&g_requested_vsync_enabled) != 0 ? 1 : 0;
            if (g_applied_vsync_enabled != vsync_enabled) {
                (void)SDL_SetRenderVSync(g_sdl_renderer, vsync_enabled);
                g_applied_vsync_enabled = vsync_enabled;
            }

            SDL_RenderClear(g_sdl_renderer);
            SDL_RenderTexture(g_sdl_renderer, texture, nullptr, nullptr);
            if (draw_cursor) {
                Render_Cursor_Overlay();
            }
            debug_overlay::Render_Published_Frame(g_sdl_renderer, g_present_generation);
            if (antares::ui::Render(g_sdl_renderer)) {
                Request_Present_Refresh();
            }
            Save_Pending_Screenshot();
            SDL_RenderPresent(g_sdl_renderer);
        }

        void Copy_Exit_Message(const char* message) {
            if (message == nullptr) {
                g_exit_message[0] = '\0';
                return;
            }

            strlcpy(g_exit_message, message, sizeof(g_exit_message));
        }

        bool Ensure_Exit_Primitives() {
            if (g_exit_mutex == nullptr) {
                g_exit_mutex = SDL_CreateMutex();
            }

            if (g_exit_condition == nullptr) {
                g_exit_condition = SDL_CreateCondition();
            }

            return g_exit_mutex != nullptr && g_exit_condition != nullptr;
        }

        void Store_Exit_Request(const char* message, int status) {
            Ensure_Exit_Primitives();

            if (g_exit_mutex != nullptr) {
                SDL_LockMutex(g_exit_mutex);
            }

            const bool first_request = SDL_GetAtomicInt(&g_exit_requested) == 0;
            if (first_request ||
                (status != EXIT_SUCCESS && message != nullptr && message[0] != '\0') ||
                (g_exit_message[0] == '\0' && message != nullptr && message[0] != '\0')) {
                Copy_Exit_Message(message);
            }

            if (first_request || status != EXIT_SUCCESS) {
                SDL_SetAtomicInt(&g_exit_status, status);
            }
            SDL_SetAtomicInt(&g_exit_requested, 1);

            if (g_exit_condition != nullptr) {
                SDL_BroadcastCondition(g_exit_condition);
            }

            if (g_exit_mutex != nullptr) {
                SDL_UnlockMutex(g_exit_mutex);
            }
        }

        void Request_SDL_Exit() {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL requested exit");
            Request_Exit();
        }

        bool Toggle_Fullscreen_Mode() {
            if (g_sdl_window == nullptr) {
                return false;
            }

            const bool fullscreen_enabled = (SDL_GetWindowFlags(g_sdl_window) & SDL_WINDOW_FULLSCREEN) != 0;
            if (!SDL_SetWindowFullscreen(g_sdl_window, !fullscreen_enabled)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_SetWindowFullscreen failed: %s", SDL_GetError());
                return false;
            }

            Request_Present_Refresh();
            Sync_Mouse_State_From_SDL();
            return true;
        }

        void Queue_Key_Code(uint32_t key_code, uint32_t scan_code) {
            key::Push_Key_Code(key_code, scan_code);
        }

        bool Translate_Non_Character_Key(SDL_Keycode keycode, uint32_t* translated_key_code) {
            switch (keycode) {
                case SDLK_ESCAPE: *translated_key_code = 0x011B; return true;
                case SDLK_BACKSPACE: *translated_key_code = 0x0E08; return true;
                case SDLK_TAB: *translated_key_code = 0x0F09; return true;
                case SDLK_RETURN: *translated_key_code = 0x1C0D; return true;
                case SDLK_PAGEUP: *translated_key_code = key::KEY_PAGE_UP; return true;
                case SDLK_PAGEDOWN: *translated_key_code = key::KEY_PAGE_DOWN; return true;
                case SDLK_END: *translated_key_code = 0x4FE0; return true;
                case SDLK_HOME: *translated_key_code = 0x47E0; return true;
                case SDLK_LEFT: *translated_key_code = 0x4BE0; return true;
                case SDLK_UP: *translated_key_code = 0x48E0; return true;
                case SDLK_RIGHT: *translated_key_code = 0x4DE0; return true;
                case SDLK_DOWN: *translated_key_code = 0x50E0; return true;
                case SDLK_INSERT: *translated_key_code = 0x52E0; return true;
                case SDLK_DELETE: *translated_key_code = 0x10000; return true;
                case SDLK_KP_PLUS: *translated_key_code = 0x0D2B; return true;
                case SDLK_KP_MINUS: *translated_key_code = 0x0C2D; return true;
                case SDLK_F1: *translated_key_code = 0x3B00; return true;
                case SDLK_F2: *translated_key_code = 0x3C00; return true;
                case SDLK_F3: *translated_key_code = 0x3D00; return true;
                case SDLK_F4: *translated_key_code = 0x3E00; return true;
                case SDLK_F5: *translated_key_code = 0x3F00; return true;
                case SDLK_F6: *translated_key_code = 0x4000; return true;
                case SDLK_F7: *translated_key_code = 0x4100; return true;
                case SDLK_F8: *translated_key_code = 0x4200; return true;
                case SDLK_F9: *translated_key_code = 0x4300; return true;
                case SDLK_F10: *translated_key_code = 0x4400; return true;
                case SDLK_F11: *translated_key_code = 0xFFFF8500; return true;
                case SDLK_F12: *translated_key_code = 0xFFFF8600; return true;
                default:
                    return false;
            }
        }

        bool Translate_Alt_Key(SDL_Keycode keycode, uint32_t* translated_key_code) {
            switch (keycode) {
                case SDLK_A: *translated_key_code = 0x1E00; return true;
                case SDLK_B: *translated_key_code = 0x3000; return true;
                case SDLK_C: *translated_key_code = 0x2E00; return true;
                case SDLK_D: *translated_key_code = 0x2000; return true;
                case SDLK_E: *translated_key_code = 0x1200; return true;
                case SDLK_F: *translated_key_code = 0x2100; return true;
                case SDLK_G: *translated_key_code = 0x2200; return true;
                case SDLK_H: *translated_key_code = 0x2300; return true;
                case SDLK_I: *translated_key_code = 0x1700; return true;
                case SDLK_J: *translated_key_code = 0x2400; return true;
                case SDLK_K: *translated_key_code = 0x2500; return true;
                case SDLK_L: *translated_key_code = 0x2600; return true;
                case SDLK_M: *translated_key_code = 0x3200; return true;
                case SDLK_N: *translated_key_code = 0x3100; return true;
                case SDLK_O: *translated_key_code = 0x1800; return true;
                case SDLK_P: *translated_key_code = 0x1900; return true;
                case SDLK_Q: *translated_key_code = 0x1000; return true;
                case SDLK_R: *translated_key_code = 0x1300; return true;
                case SDLK_S: *translated_key_code = 0x1F00; return true;
                case SDLK_T: *translated_key_code = 0x1400; return true;
                case SDLK_U: *translated_key_code = 0x1600; return true;
                case SDLK_V: *translated_key_code = 0x2F00; return true;
                case SDLK_W: *translated_key_code = 0x1100; return true;
                case SDLK_X: *translated_key_code = 0x2D00; return true;
                case SDLK_Y: *translated_key_code = 0x1500; return true;
                case SDLK_Z: *translated_key_code = 0x2C00; return true;
                case SDLK_F1: *translated_key_code = 0x6800; return true;
                case SDLK_F2: *translated_key_code = 0x6900; return true;
                case SDLK_F3: *translated_key_code = 0x6A00; return true;
                case SDLK_F4: *translated_key_code = 0x6B00; return true;
                case SDLK_F5: *translated_key_code = 0x6C00; return true;
                case SDLK_F6: *translated_key_code = 0x6D00; return true;
                case SDLK_F7: *translated_key_code = 0x6E00; return true;
                case SDLK_F8: *translated_key_code = 0x6F00; return true;
                case SDLK_F9: *translated_key_code = 0x7000; return true;
                case SDLK_F10: *translated_key_code = 0x7100; return true;
                default:
                    return false;
            }
        }

        bool Translate_Printable_Key(SDL_Keycode keycode, SDL_Keymod modifiers, uint32_t* translated_character) {
            const bool shift_down = (modifiers & SDL_KMOD_SHIFT) != 0;

            if (keycode >= SDLK_A && keycode <= SDLK_Z) {
                *translated_character = shift_down ? (uint32_t)(keycode - 0x20) : (uint32_t)keycode;
                return true;
            }

            if (!shift_down) {
                if (keycode >= 0x20 && keycode <= 0x7E) {
                    *translated_character = (uint32_t)keycode;
                    return true;
                }
                return false;
            }

            switch (keycode) {
                case SDLK_1: *translated_character = '!'; return true;
                case SDLK_2: *translated_character = '@'; return true;
                case SDLK_3: *translated_character = '#'; return true;
                case SDLK_4: *translated_character = '$'; return true;
                case SDLK_5: *translated_character = '%'; return true;
                case SDLK_6: *translated_character = '^'; return true;
                case SDLK_7: *translated_character = '&'; return true;
                case SDLK_8: *translated_character = '*'; return true;
                case SDLK_9: *translated_character = '('; return true;
                case SDLK_0: *translated_character = ')'; return true;
                case SDLK_MINUS: *translated_character = '_'; return true;
                case SDLK_EQUALS: *translated_character = '+'; return true;
                case SDLK_LEFTBRACKET: *translated_character = '{'; return true;
                case SDLK_RIGHTBRACKET: *translated_character = '}'; return true;
                case SDLK_BACKSLASH: *translated_character = '|'; return true;
                case SDLK_SEMICOLON: *translated_character = ':'; return true;
                case SDLK_APOSTROPHE: *translated_character = '"'; return true;
                case SDLK_COMMA: *translated_character = '<'; return true;
                case SDLK_PERIOD: *translated_character = '>'; return true;
                case SDLK_SLASH: *translated_character = '?'; return true;
                case SDLK_GRAVE: *translated_character = '~'; return true;
                default:
                    if (keycode >= 0x20 && keycode <= 0x7E) {
                        *translated_character = (uint32_t)keycode;
                        return true;
                    }
                    return false;
            }
        }

        void Handle_SDL_Key_Down(const SDL_KeyboardEvent& key_event) {
            if (key_event.repeat) {
                return;
            }

            uint32_t translated_key_code = 0;
            const uint32_t scan_code = key_event.raw;

            if ((key_event.mod & SDL_KMOD_ALT) != 0 &&
                (key_event.key == SDLK_RETURN || key_event.key == SDLK_KP_ENTER)) {
                Toggle_Fullscreen_Mode();
                return;
            }

            if ((key_event.mod & SDL_KMOD_ALT) != 0) {
                if (Translate_Alt_Key(key_event.key, &translated_key_code)) {
                    Enqueue_Key_Input_Event(translated_key_code, scan_code);
                }
                return;
            }

            if (Translate_Non_Character_Key(key_event.key, &translated_key_code)) {
                Enqueue_Key_Input_Event(translated_key_code, scan_code);
                return;
            }

            if (Translate_Printable_Key(key_event.key, key_event.mod, &translated_key_code)) {
                Enqueue_Key_Input_Event(translated_key_code + (scan_code << 8), scan_code);
            }
        }

        void Present_Published_Off_Page() {
            if (Update_Active_Screen_Frame()) {
                Request_Present_Refresh();
            }

            if (g_applied_present_scaler != Requested_Present_Scaler()) {
                Request_Present_Refresh();
            }

            if (!Consume_Present_Refresh_Request()) {
                return;
            }

            Present_Indexed_Buffer(g_present_surface);
        }

        bool Ensure_Present_Palette() {
            if (g_sdl_present_palette != nullptr) {
                return true;
            }

            g_sdl_present_palette = SDL_CreatePalette(256);
            return g_sdl_present_palette != nullptr;
        }

        bool Ensure_Cursor_Surface() {
            if (g_sdl_cursor_surface != nullptr) {
                return true;
            }

            if (!Ensure_Present_Palette() || g_cursor_surface_1 == nullptr) {
                return false;
            }

            SDL_Surface* cursor_surface = SDL_CreateSurfaceFrom(
                k_cursor_dimension,
                k_cursor_dimension,
                SDL_PIXELFORMAT_INDEX8,
                g_cursor_surface_1,
                k_cursor_dimension);
            if (cursor_surface == nullptr) {
                return false;
            }

            if (!SDL_SetSurfacePalette(cursor_surface, g_sdl_present_palette) ||
                !SDL_SetSurfaceColorKey(cursor_surface, true, 0)) {
                SDL_DestroySurface(cursor_surface);
                return false;
            }

            g_sdl_cursor_surface = cursor_surface;
            return true;
        }

        void Apply_Pending_Cursor_State() {
            if (g_present_cursor_mutex == nullptr ||
                g_cursor_surface_1 == nullptr ||
                cursor_surface == nullptr) {
                return;
            }

            SDL_LockMutex(g_present_cursor_mutex);
            if (g_pending_cursor_generation != g_applied_cursor_generation) {
                g_active_cursor_state.offset = g_pending_cursor_state.offset;
                g_active_cursor_state.shape = g_pending_cursor_state.shape;
                g_active_cursor_state.enabled = g_pending_cursor_state.enabled;
                g_active_cursor_state.pixels = g_cursor_surface_1;
                memcpy(g_cursor_surface_1, cursor_surface, k_cursor_pixel_count);
                g_applied_cursor_generation = g_pending_cursor_generation;
            }
            SDL_UnlockMutex(g_present_cursor_mutex);
        }

        bool Ensure_Present_Backend() {
            if (g_sdl_renderer != nullptr &&
                g_sdl_present_palette != nullptr &&
                g_sdl_screen_index_surface != nullptr &&
                g_sdl_present_argb_surface != nullptr &&
                g_sdl_present_pixels != nullptr) {
                return Ensure_Scaler_Output();
            }

            if (g_sdl_renderer == nullptr) {
                g_sdl_renderer = SDL_CreateRenderer(g_sdl_window, nullptr);
                if (g_sdl_renderer == nullptr) {
                    g_sdl_renderer = SDL_CreateRenderer(g_sdl_window, "software");
                }
                if (g_sdl_renderer == nullptr) {
                    return false;
                }

                g_applied_vsync_enabled = -1;
            }

            if (!Ensure_Render_Presentation()) {
                return false;
            }

            const size_t pixel_count =
                (size_t)video::g_screen_res_x *
                (size_t)video::g_screen_res_y;
            if (g_sdl_present_pixels == nullptr) {
                g_sdl_present_pixels = (uint32_t*)malloc(pixel_count * sizeof(uint32_t));
                if (g_sdl_present_pixels != nullptr) {
                    memset(g_sdl_present_pixels, 0, pixel_count * sizeof(uint32_t));
                }
            }
            if (g_sdl_present_pixels == nullptr) {
                return false;
            }

            if (g_present_scaler_pipeline.implementation == nullptr &&
                !scalers::Initialize(
                    &g_present_scaler_pipeline,
                    video::g_screen_res_x,
                    video::g_screen_res_y)) {
                return false;
            }

            if (!Ensure_Present_Palette()) {
                return false;
            }

            if (g_sdl_screen_index_surface == nullptr) {
                if (g_present_surface == nullptr) {
                    return false;
                }

                SDL_Surface* screen_index_surface = SDL_CreateSurfaceFrom(
                    video::g_screen_res_x,
                    video::g_screen_res_y,
                    SDL_PIXELFORMAT_INDEX8,
                    g_present_surface,
                    video::g_screen_res_x);
                if (screen_index_surface == nullptr) {
                    return false;
                }

                if (!SDL_SetSurfacePalette(screen_index_surface, g_sdl_present_palette)) {
                    SDL_DestroySurface(screen_index_surface);
                    return false;
                }

                g_sdl_screen_index_surface = screen_index_surface;
            }

            if (g_sdl_present_argb_surface == nullptr) {
                g_sdl_present_argb_surface = SDL_CreateSurfaceFrom(
                    video::g_screen_res_x,
                    video::g_screen_res_y,
                    SDL_PIXELFORMAT_ARGB8888,
                    g_sdl_present_pixels,
                    video::g_screen_res_x * (int32_t)sizeof(uint32_t));
                if (g_sdl_present_argb_surface == nullptr) {
                    return false;
                }
            }

            Ensure_Cursor_Surface();
            return Ensure_Scaler_Output();
        }

        void Destroy_Present_Backend() {
            Destroy_Scaler_Output();
            scalers::Shutdown(&g_present_scaler_pipeline);

            if (g_sdl_cursor_surface != nullptr) {
                SDL_DestroySurface(g_sdl_cursor_surface);
                g_sdl_cursor_surface = nullptr;
            }

            if (g_sdl_present_argb_surface != nullptr) {
                SDL_DestroySurface(g_sdl_present_argb_surface);
                g_sdl_present_argb_surface = nullptr;
            }

            if (g_sdl_screen_index_surface != nullptr) {
                SDL_DestroySurface(g_sdl_screen_index_surface);
                g_sdl_screen_index_surface = nullptr;
            }

            if (g_sdl_present_palette != nullptr) {
                SDL_DestroyPalette(g_sdl_present_palette);
                g_sdl_present_palette = nullptr;
            }

            if (g_sdl_renderer != nullptr) {
                SDL_DestroyRenderer(g_sdl_renderer);
                g_sdl_renderer = nullptr;
            }
            g_applied_vsync_enabled = -1;
            g_applied_present_scaler = (e_present_scaler)UINT8_MAX;

            if (g_sdl_present_pixels != nullptr) {
                free(g_sdl_present_pixels);
                g_sdl_present_pixels = nullptr;
            }
        }

        void Update_SDL_Palette() {
            if (g_sdl_present_palette == nullptr) {
                return;
            }

            SDL_Color colors[256];
            for (int32_t i = 0; i < 256; ++i) {
                colors[i].r = g_present_palette_entries[i].r;
                colors[i].g = g_present_palette_entries[i].g;
                colors[i].b = g_present_palette_entries[i].b;
                colors[i].a = 255;
            }

            (void)SDL_SetPaletteColors(g_sdl_present_palette, colors, 0, 256);
        }

        // Test input script captures (see the script commands further down).
        std::string g_pending_screenshot_hd;
        std::string g_pending_screenshot_page;

        // Outcomes of waits and screenshots since the last control reply.
        std::vector<std::string> g_control_results;

        void Note_Script_Result(const std::string& result) {
            if (antares::control::Enabled()) {
                g_control_results.push_back(result);
            }
        }

        uint64_t g_presented_frames;

        // HD presentation: the 4x HD surface of the presented page, coloured with its palette.
        SDL_Texture* g_hd_texture;
        uint32_t* g_hd_pixels;
        // False until the HD texture holds a complete frame (first frame, failed upload, window
        // exposed or renderer reset).
        bool g_hd_frame_valid;

        bool Present_HD_Frame() {
            const int32_t width = video::g_screen_res_x * render::SCALE;
            const int32_t height = video::g_screen_res_y * render::SCALE;
            if (g_hd_texture == nullptr) {
                g_hd_texture = SDL_CreateTexture(g_sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING, width, height);
                g_hd_pixels = (uint32_t*)calloc((size_t)width * (size_t)height, sizeof(uint32_t));
                if (g_hd_texture == nullptr || g_hd_pixels == nullptr) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: unable to create the HD frame: %s", SDL_GetError());
                    return false;
                }
                (void)SDL_SetTextureScaleMode(g_hd_texture, SDL_SCALEMODE_LINEAR);
            }

            // The HD surface rows that changed since the last present, in the presented palette
            // (everything when the palette changed).
            const uint64_t resolve_start = SDL_GetPerformanceCounter();
            int32_t row_first = 0;
            int32_t row_last = 0;
            const bool changed = render::Resolve(g_present_palette_entries, g_hd_pixels, &row_first, &row_last);
            static double resolve_ms_total;
            static uint32_t resolve_count;
            resolve_ms_total += (double)(SDL_GetPerformanceCounter() - resolve_start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
            if (++resolve_count % 200 == 0 && antares::Get_Env("HD_STATS") != nullptr) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd stats: resolve %.2f ms average over %u frames",
                    resolve_ms_total / resolve_count, resolve_count);
            }

            if (!g_pending_screenshot_hd.empty()) {
                const bool saved = antares::script_capture::Save_Argb_Png(g_pending_screenshot_hd.c_str(), g_hd_pixels, width, height, width);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: screenshot_hd %s%s", g_pending_screenshot_hd.c_str(), saved ? "" : " FAILED");
                Note_Script_Result("screenshot_hd " + g_pending_screenshot_hd + (saved ? " ok" : " FAILED"));
                render::Parity_Save(g_pending_screenshot_hd.c_str());
                g_pending_screenshot_hd.clear();
            }
            // Debug aid for headless runs: ANTARES_HD_DUMP=<directory> saves every 50th
            // presented HD frame as a BMP.
            static uint32_t hd_frame_counter;
            const char* dump_dir = antares::Get_Env("HD_DUMP");
            if (dump_dir != nullptr && (hd_frame_counter++ % 50) == 0) {
                SDL_Surface* dump = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888,
                    g_hd_pixels, width * (int32_t)sizeof(uint32_t));
                char path[512];
                snprintf(path, sizeof(path), "%s/hd_%05u.bmp", dump_dir, hd_frame_counter);
                if (dump != nullptr) {
                    (void)SDL_SaveBMP(dump, path);
                    SDL_DestroySurface(dump);
                }
            }
            if (antares::host::Frame_Resolved(g_hd_pixels, width, height, changed, row_first, row_last)) {
                return true;
            }
            bool uploaded = true;
            if (!g_hd_frame_valid) {
                uploaded = SDL_UpdateTexture(g_hd_texture, nullptr, g_hd_pixels, width * (int32_t)sizeof(uint32_t));
            } else if (changed) {
                const SDL_Rect area = {0, row_first, width, row_last - row_first};
                uploaded = SDL_UpdateTexture(g_hd_texture, &area, g_hd_pixels + (size_t)row_first * width,
                    width * (int32_t)sizeof(uint32_t));
            }
            if (!uploaded) {
                g_hd_frame_valid = false;
                return false;
            }
            g_hd_frame_valid = true;
            Present_Texture_To_Output(g_hd_texture, true);
            return true;
        }

        void Present_Indexed_Buffer(const uint8_t* src_pixels) {
            if (src_pixels == nullptr || !Ensure_Present_Backend()) {
                return;
            }

            Update_SDL_Palette();
            if (!SDL_BlitSurface(g_sdl_screen_index_surface, nullptr, g_sdl_present_argb_surface, nullptr)) {
                return;
            }
            ++g_presented_frames;
            if (!g_pending_screenshot_page.empty()) {
                const bool saved = antares::script_capture::Save_Argb_Png(g_pending_screenshot_page.c_str(), (const uint32_t*)g_sdl_present_argb_surface->pixels,
                    g_sdl_present_argb_surface->w, g_sdl_present_argb_surface->h, g_sdl_present_argb_surface->pitch / (int32_t)sizeof(uint32_t));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: screenshot_page %s%s", g_pending_screenshot_page.c_str(), saved ? "" : " FAILED");
                Note_Script_Result("screenshot_page " + g_pending_screenshot_page + (saved ? " ok" : " FAILED"));
                g_pending_screenshot_page.clear();
            }

            // Antares (WP5.5): with an HD art pack the HD frame is presented and the scaler's output
            // would be thrown away, so the scaler only runs when the HD frame can't be shown. Its
            // dirty-region state is reset so that a fallback frame is scaled whole.
            if (render::Enabled()) {
                if (Present_HD_Frame()) {
                    scalers::Invalidate(&g_present_scaler_pipeline);
                    return;
                }
            } else {
                Blit_Cursor_On_Present_Surface();
                if (!g_pending_screenshot_hd.empty()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "input script: screenshot_hd %s FAILED (no HD pack enabled)", g_pending_screenshot_hd.c_str());
                    Note_Script_Result("screenshot_hd " + g_pending_screenshot_hd + " FAILED (no HD pack)");
                    g_pending_screenshot_hd.clear();
                }
            }
            if (!scalers::Process(
                    &g_present_scaler_pipeline,
                    g_sdl_present_pixels,
                    video::g_screen_res_x)) {
                return;
            }

            const int32_t dirty_region_count =
                scalers::Get_Dirty_Region_Count(&g_present_scaler_pipeline);
            const scalers::s_region* dirty_regions =
                scalers::Get_Dirty_Regions(&g_present_scaler_pipeline);
            const uint32_t* output_pixels =
                scalers::Get_Output_Pixels(&g_present_scaler_pipeline);
            const int32_t output_stride =
                scalers::Get_Output_Stride(&g_present_scaler_pipeline);
            bool updated = true;
            for (int32_t region_index = 0;
                 region_index < dirty_region_count && updated;
                 ++region_index) {
                const scalers::s_region& region = dirty_regions[region_index];
                const SDL_Rect output_region = {
                    region.x,
                    region.y,
                    region.width,
                    region.height,
                };
                updated = SDL_UpdateTexture(
                    g_sdl_present_texture,
                    &output_region,
                    output_pixels + region.y * output_stride + region.x,
                    output_stride * (int32_t)sizeof(uint32_t));
            }

            if (!updated) {
                scalers::Invalidate(&g_present_scaler_pipeline);
            }

            Present_Texture_To_Output(g_sdl_present_texture);
        }

        // With HD sprites the cursor is drawn as its own texture over the composed frame, so the
        // area under it is not left on the low-resolution base image.
        void Render_Cursor_Overlay() {
            if (g_sdl_renderer == nullptr || debug_overlay::Visible()) {
                return;
            }
            Apply_Pending_Cursor_State();
            const s_present_cursor_state& cursor_state = g_active_cursor_state;
            if (cursor_state.pixels == nullptr || cursor_state.enabled == 0 || cursor_state.shape <= 0 ||
                !Ensure_Cursor_Surface()) {
                return;
            }
            // HD cursor art (looked up by the cursor image's fingerprint), else the original.
            static uint32_t hd_cursor[k_cursor_dimension * render::SCALE * k_cursor_dimension * render::SCALE];
            SDL_Texture* cursor = nullptr;
            if (render::Enabled() &&
                render::Cursor_Art(cursor_state.pixels, k_cursor_dimension, g_present_palette_entries, hd_cursor)) {
                const int32_t size = k_cursor_dimension * render::SCALE;
                cursor = SDL_CreateTexture(g_sdl_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, size, size);
                if (cursor != nullptr) {
                    (void)SDL_UpdateTexture(cursor, nullptr, hd_cursor, size * (int32_t)sizeof(uint32_t));
                    (void)SDL_SetTextureBlendMode(cursor, SDL_BLENDMODE_BLEND);
                    (void)SDL_SetTextureScaleMode(cursor, SDL_SCALEMODE_LINEAR);
                }
            }
            if (cursor == nullptr) {
                cursor = SDL_CreateTextureFromSurface(g_sdl_renderer, g_sdl_cursor_surface);
                if (cursor == nullptr) {
                    return;
                }
                (void)SDL_SetTextureScaleMode(cursor, SDL_SCALEMODE_PIXELART);
            }
            const SDL_FRect destination = {(float)SDL_GetAtomicInt(&g_present_mouse_x),
                (float)SDL_GetAtomicInt(&g_present_mouse_y), (float)k_cursor_dimension, (float)k_cursor_dimension};
            SDL_RenderTexture(g_sdl_renderer, cursor, nullptr, &destination);
            SDL_DestroyTexture(cursor);
        }

        void Blit_Cursor_On_Present_Surface() {
            if (g_sdl_present_argb_surface == nullptr || debug_overlay::Visible()) {
                return;
            }

            Apply_Pending_Cursor_State();
            const s_present_cursor_state& cursor_state = g_active_cursor_state;

            if (cursor_state.pixels == nullptr ||
                cursor_state.enabled == 0 ||
                cursor_state.shape <= 0) {
                return;
            }

            if (!Ensure_Cursor_Surface()) {
                return;
            }

            const int32_t dst_x0 =
                SDL_GetAtomicInt(&g_present_mouse_x);
            const int32_t dst_y0 =
                SDL_GetAtomicInt(&g_present_mouse_y);

            SDL_Rect dst_rect = {dst_x0, dst_y0, k_cursor_dimension, k_cursor_dimension};
            (void)SDL_BlitSurface(
                g_sdl_cursor_surface,
                nullptr,
                g_sdl_present_argb_surface,
                &dst_rect);
        }

        void Sync_Mouse_State_From_SDL() {
            if (g_sdl_window == nullptr || g_window_focus_state == 0) {
                return;
            }

            float mouse_x = 0.0f;
            float mouse_y = 0.0f;
            const SDL_MouseButtonFlags sdl_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

            int16_t mapped_mouse_x = 0;
            int16_t mapped_mouse_y = 0;
            Map_Window_Point_To_Game_Point(mouse_x, mouse_y, &mapped_mouse_x, &mapped_mouse_y);
            int32_t raw_width = 0;
            int32_t raw_height = 0;
            Get_Raw_Screen_Resolution(&raw_width, &raw_height);

            uint16_t mapped_buttons = 0;
            if ((sdl_buttons & SDL_BUTTON_LMASK) != 0) {
                mapped_buttons |= 1;
            }
            if ((sdl_buttons & SDL_BUTTON_RMASK) != 0) {
                mapped_buttons |= 2;
            }

            Set_Present_Mouse_Position(mapped_mouse_x, mapped_mouse_y);
            const bool overlay_captures_mouse =
                debug_overlay::Handle_Mouse_Position(
                    mapped_mouse_x,
                    mapped_mouse_y,
                    mouse_x,
                    mouse_y,
                    raw_width,
                    raw_height);

            if (mapped_mouse_x != platform::Get_Mouse_X() ||
                mapped_mouse_y != platform::Get_Mouse_Y() ||
                mapped_buttons != g_sdl_mouse_buttons ||
                overlay_captures_mouse != g_game_mouse_suppressed) {
                g_sdl_mouse_buttons = mapped_buttons;
                Forward_Game_Mouse_State(mapped_mouse_x, mapped_mouse_y, mapped_buttons, overlay_captures_mouse);
            }
        }
    } // namespace

    uint32_t g_os_ver;
    uint32_t g_win_minor;
    uint32_t g_win_major;
    uint32_t g_win_ver;
    char* g_command_line;
    void* g_env_strings;
    SDL_Thread* g_worker_thread_handle;
    uint32_t updating_cursor_img;
    char g_exit_message[k_exit_message_size];
    void* g_main_window;
    volatile bool g_pending_toggle_pages;
    int16_t g_video_mode;
    uint32_t g_available_mem;
    uint8_t* g_present_surface;
    uint8_t* g_cursor_surface_1;
    uint8_t* cursor_surface;
    uint32_t g_frame_count;
    int8_t g_render_buffer;
    uint8_t g_vsync_enabled = 1;
    e_present_scaler g_present_scaler = PRESENT_SCALER_PIXELART;
    int32_t g_window_width = 640;
    int32_t g_window_height = 480;
    uint8_t g_fullscreen = 0;
    uint8_t* offscreen_surface;
    float g_raw_mouse_x;
    float g_raw_mouse_y;
    int32_t g_raw_screen_res_x;
    int32_t g_raw_screen_res_y;
    int32_t g_last_cursor_shape;
    int16_t g_mouse_x;
    int16_t g_mouse_y;

    constexpr int32_t k_windowed_logical_width = 640;
    constexpr int32_t k_windowed_logical_height = 480;

    bool __cdecl Init_Exit() {
        const bool initialized = Ensure_Exit_Primitives();
        Reset_Exit();
        return initialized;
    }

    void __cdecl Shutdown_Exit() {
        if (g_exit_condition != nullptr) {
            SDL_DestroyCondition(g_exit_condition);
            g_exit_condition = nullptr;
        }

        if (g_exit_mutex != nullptr) {
            SDL_DestroyMutex(g_exit_mutex);
            g_exit_mutex = nullptr;
        }

        SDL_SetAtomicInt(&g_exit_requested, 0);
        SDL_SetAtomicInt(&g_exit_status, EXIT_SUCCESS);
        SDL_SetAtomicInt(&g_game_thread_exited, 0);
        g_exit_message[0] = '\0';
    }

    void __cdecl Reset_Exit() {
        Ensure_Exit_Primitives();

        if (g_exit_mutex != nullptr) {
            SDL_LockMutex(g_exit_mutex);
        }

        SDL_SetAtomicInt(&g_exit_requested, 0);
        SDL_SetAtomicInt(&g_exit_status, EXIT_SUCCESS);
        SDL_SetAtomicInt(&g_game_thread_exited, 0);
        g_exit_message[0] = '\0';

        if (g_exit_condition != nullptr) {
            SDL_BroadcastCondition(g_exit_condition);
        }

        if (g_exit_mutex != nullptr) {
            SDL_UnlockMutex(g_exit_mutex);
        }
    }

    void __cdecl Request_Exit(const char* message) {
        Store_Exit_Request(message, EXIT_SUCCESS);
    }

    void __cdecl Request_Fatal_Exit(const char* message) {
        Store_Exit_Request(message, EXIT_FAILURE);
    }

    bool __cdecl Exit_Requested() {
        return SDL_GetAtomicInt(&g_exit_requested) != 0;
    }

    const char* __cdecl Exit_Message() {
        return g_exit_message;
    }

    int __cdecl Exit_Status() {
        return SDL_GetAtomicInt(&g_exit_status);
    }

    void __cdecl Check_Exit() {
        if (Exit_Requested()) {
            throw process_exit_requested{};
        }
    }

    void __cdecl Mark_Game_Thread_Exited() {
        SDL_SetAtomicInt(&g_game_thread_exited, 1);

        if (g_exit_mutex != nullptr) {
            SDL_LockMutex(g_exit_mutex);
        }

        if (g_exit_condition != nullptr) {
            SDL_BroadcastCondition(g_exit_condition);
        }

        if (g_exit_mutex != nullptr) {
            SDL_UnlockMutex(g_exit_mutex);
        }
    }

    bool __cdecl Game_Thread_Exited() {
        return SDL_GetAtomicInt(&g_game_thread_exited) != 0;
    }

    bool cleanup() {
        Prepare_Exit();
        Destroy_Main_Window();
        return false;
    }

    bool Create_Video_Buffers(int16_t video_mode) {
        s_memorystatus mem_status;
        Query_Memory_Status(&mem_status);

        g_video_mode = video_mode;
        g_available_mem = mem_status.dwAvailPhys;

        switch (video_mode) {
            case 0:
                video::g_screen_res_x = 320;
                video::g_screen_res_y = 200;
                break;
            case 2:
                video::g_screen_res_x = 320;
                video::g_screen_res_y = 240;
                break;
            case 4:
                video::g_screen_res_x = 640;
                video::g_screen_res_y = 480;
                break;
            case 5:
                video::g_screen_res_x = k_windowed_logical_width;
                video::g_screen_res_y = k_windowed_logical_height;
            default:
                break;
        }

        video::screen_pixel_size = video::g_screen_res_x * video::g_screen_res_y;
        video::Allocate_Refresh_Lists(video::screen_pixel_size, video::g_screen_res_y);
        video::Allocate_Page_Buffers(video::g_screen_res_x, video::g_screen_res_y);
        video::Init_Off_Page_Ownership();

        g_present_surface = (uint8_t*)malloc((size_t)video::g_screen_res_x * (size_t)video::g_screen_res_y);
        if (g_present_surface == nullptr) {
            return cleanup();
        }

        memset(g_present_surface, 0, (size_t)video::g_screen_res_x * (size_t)video::g_screen_res_y);

        video::Set_Page_Off();
        graphics::Fill(0, 0, 639, 479, 0);
        video::Set_Page_Back();
        graphics::Fill(0, 0, 639, 479, 0);
        video::Clear_Scan_List();
        video::Clear_Stencil_List();
        video::screen_refresh_flag = 0;
        graphics::Reset_Window();
        return true;
    }

    void Restore_Surfaces() {
        g_frame_count = 0;
        if (video::screen_refresh_flag == 3) {
            video::Set_Refresh_Stencil();
        }
    }

    void __cdecl Prepare_Exit() {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: Prepare_Exit");
        Request_Exit();
    }

    void Cleanup_Surfaces() {
        // HD art decoding threads use SDL; they must finish before SDL shuts down.
        render::Shutdown();
        Destroy_Present_Backend();

        uint8_t* const primary_cursor_surface = g_cursor_surface_1;
        uint8_t* const alternate_cursor_surface = cursor_surface;

        if (g_present_surface != nullptr) {
            free(g_present_surface);
            g_present_surface = nullptr;
        }

        if (primary_cursor_surface != nullptr) {
            free(primary_cursor_surface);
            g_cursor_surface_1 = nullptr;
        }

        if (alternate_cursor_surface != nullptr && alternate_cursor_surface != primary_cursor_surface) {
            free(alternate_cursor_surface);
        }
        cursor_surface = nullptr;

        g_pending_cursor_state = {};
        g_active_cursor_state = {};
        g_pending_cursor_generation = 0;
        g_applied_cursor_generation = 0;

        video::Free_Page_Buffers();
        video::Shutdown_Off_Page_Ownership();
    }

    void Destroy_Main_Window() {
        if (g_sdl_window != nullptr) {
            Destroy_Present_Backend();
            SDL_DestroyWindow(g_sdl_window);
            g_sdl_window = nullptr;
        }

        g_main_window = nullptr;
    }

    bool __cdecl Update_Active_Screen_Frame() {
        if (!video::Try_Begin_Off_Page_Present()) {
            return false;
        }

        // Copy the published Off page into a separate presenter-owned surface.
        // The gameplay Back page remains a stable software page and is not part of present-page alternation.
        Copy_Off_Page_To_Present_Surface();
        video::End_Off_Page_Present();
        g_present_generation = video::g_last_presented_generation;
        g_frame_count++;
        return true;
    }

    namespace {
        // ANTARES_INPUT_SCRIPT=<file> (or ORION2RE_INPUT_SCRIPT): replays timed input for automated testing. One command
        // per line, '#' comments:
        //   wait <ms>           pause before the next command
        //   move <x> <y>        move the mouse (640x480 game coordinates)
        //   click <x> <y>       move, press and release the left button
        //   rclick <x> <y>      same with the right button
        //   key <name>          press a key (SDL key name, e.g. Return, Escape, a)
        //   wheel <notches>     turn the mouse wheel (positive = up, away from the user)
        //   event <name>        window event: focus_lost, focus_gained, exposed
        //   screenshot <file>   save the next presented window as BMP
        //   screenshot_hd <file.png>    save the next 4x HD frame (2560x1920), any window size
        //   screenshot_page <file.png>  save the next 640x480 page in its palette
        //   wait_frames <n>     pause until n more frames have been presented
        //   expect_screen <lbx> [ms]    pause until <lbx> loads after the last input (default
        //                       30000 ms); logs "expect_screen ... ok" or "... FAILED"
        //   wait_input [n] [ms] pause until the game has polled input n times (default 10) with no
        //                       LBX load or change of field count in between: the screen (or
        //                       dialog) has settled and takes clicks
        //                       (default timeout 30000 ms)
        //   log <word>          write a marker to the log (runner section names)
        //   quit                request exit
        // ANTARES_CONTROL=<socket path> (antares/control.h) takes the same commands live, one line at
        // a time, plus `state` (no input, just the reply). After each command and its waits, the game
        // answers one JSON line: {"ok", "results" (wait/expect/screenshot outcomes), "frame",
        // "polls", "fields", "screen", "stardate", "recent_lbx"}. Clicks reply after the release.
        struct s_script_step {
            std::string command;
            int32_t a;
            int32_t b;
            std::string text;
        };
        std::vector<s_script_step> g_script;
        size_t g_script_position;
        uint64_t g_script_resume_ms;
        bool g_script_loaded;
        std::string g_pending_screenshot;
        uint64_t g_wait_frames_until;
        // expect_screen: the LBX awaited, when it started and gives up, and the LBX load serial
        // at the last input the script sent (a load must come after it).
        std::string g_expect_lbx;
        uint64_t g_expect_start_ms;
        uint64_t g_expect_deadline_ms;
        uint64_t g_last_input_serial;
        // wait_input: the poll count to reach, and when to give up.
        uint64_t g_wait_input_until;
        uint64_t g_wait_input_count;
        uint64_t g_wait_input_lbx_serial;
        uint64_t g_wait_input_layout;
        uint64_t g_wait_input_start_ms;
        uint64_t g_wait_input_deadline_ms;

        // One input-script line to a step; false for blank and comment lines.
        bool Parse_Script_Line(std::string line, s_script_step* out) {
            const size_t hash = line.find('#');
            if (hash != std::string::npos) {
                line.resize(hash);
            }
            char command[32] = {};
            char argument[256] = {};
            int a = 0;
            int b = 0;
            if (sscanf(line.c_str(), "%31s", command) != 1) {
                return false;
            }
            s_script_step step = {command, 0, 0, {}};
            if (step.command == "move" || step.command == "click" || step.command == "rclick") {
                (void)sscanf(line.c_str(), "%*s %d %d", &a, &b);
            } else if (step.command == "wait" || step.command == "wheel" || step.command == "wait_frames") {
                (void)sscanf(line.c_str(), "%*s %d", &a);
            } else if (step.command == "wait_input") {
                a = 10;
                b = 30000;
                (void)sscanf(line.c_str(), "%*s %d %d", &a, &b);
            } else if (step.command == "expect_screen") {
                a = 30000;
                (void)sscanf(line.c_str(), "%*s %255s %d", argument, &a);
                step.text = argument;
            } else if (sscanf(line.c_str(), "%*s %255s", argument) == 1) {
                step.text = argument;
            }
            step.a = a;
            step.b = b;
            *out = step;
            return true;
        }

        void Send_Control_Reply() {
            std::string json = "{\"ok\":true,\"results\":[";
            for (size_t i = 0; i < g_control_results.size(); ++i) {
                json += (i != 0 ? ",\"" : "\"") + antares::control::Json_Escape(g_control_results[i]) + "\"";
            }
            g_control_results.clear();
            json += "],\"frame\":" + std::to_string(g_presented_frames);
            json += ",\"polls\":" + std::to_string(antares::input_trace::Polls());
            json += ",\"fields\":" + std::to_string(antares::input_trace::g_last_field_count);
            json += ",\"screen\":" + std::to_string((int)MOX::current_screen);
            json += ",\"stardate\":" + std::to_string((int)MOX::stardate);
            json += ",\"recent_lbx\":[";
            const std::vector<std::string> recent = antares::lbx_trace::Recent(6);
            for (size_t i = 0; i < recent.size(); ++i) {
                json += (i != 0 ? ",\"" : "\"") + antares::control::Json_Escape(recent[i]) + "\"";
            }
            json += "]}";
            antares::control::Reply(json);
        }

        // Live control: when the queue has drained, take the next command and queue it with a reply.
        void Poll_Control() {
            if (g_script_position < g_script.size() || !antares::control::Enabled()) {
                return;
            }
            std::string line;
            if (!antares::control::Read_Line(&line)) {
                return;
            }
            s_script_step step;
            if (!Parse_Script_Line(line, &step)) {
                antares::control::Reply("{\"ok\":false,\"error\":\"empty command\"}");
                return;
            }
            if (step.command != "state") {
                g_script.push_back(step);
            }
            g_script.push_back(s_script_step{"reply", 0, 0, {}});
        }

        void Load_Input_Script() {
            g_script_loaded = true;
            const char* path = antares::Get_Env("INPUT_SCRIPT");
            if (path == nullptr) {
                return;
            }
            size_t size = 0;
            char* text = (char*)SDL_LoadFile(path, &size);
            if (text == nullptr) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "input script: cannot read %s", path);
                return;
            }
            std::string all(text, size);
            SDL_free(text);
            size_t start = 0;
            while (start < all.size()) {
                size_t end = all.find('\n', start);
                if (end == std::string::npos) {
                    end = all.size();
                }
                std::string line = all.substr(start, end - start);
                start = end + 1;
                s_script_step step;
                if (!Parse_Script_Line(line, &step)) {
                    continue;
                }
                g_script.push_back(step);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: %zu steps from %s", g_script.size(), path);
        }

        void Run_Input_Script() {
            if (!g_script_loaded) {
                Load_Input_Script();
            }
            Poll_Control();
            const uint64_t now = SDL_GetTicks();
            if (!g_expect_lbx.empty()) {
                const bool loaded = antares::lbx_trace::Last_Load(g_expect_lbx.c_str()) > g_last_input_serial;
                if (!loaded && now < g_expect_deadline_ms) {
                    return;
                }
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: expect_screen %s %s after %llu ms",
                    g_expect_lbx.c_str(), loaded ? "ok" : "FAILED", (unsigned long long)(now - g_expect_start_ms));
                Note_Script_Result("expect_screen " + g_expect_lbx + (loaded ? " ok" : " FAILED") + " " +
                                   std::to_string(now - g_expect_start_ms) + " ms");
                g_expect_lbx.clear();
            }
            if (g_wait_input_until != 0) {
                // A screen change loads files: count again from there.
                const uint64_t lbx_serial = antares::lbx_trace::Serial();
                const uint64_t layout = antares::input_trace::Layout();
                if (lbx_serial != g_wait_input_lbx_serial || layout != g_wait_input_layout) {
                    g_wait_input_lbx_serial = lbx_serial;
                    g_wait_input_layout = layout;
                    g_wait_input_until = antares::input_trace::Polls() + g_wait_input_count;
                }
                const bool polled = antares::input_trace::Polls() >= g_wait_input_until;
                if (!polled && now < g_wait_input_deadline_ms) {
                    return;
                }
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: wait_input %s after %llu ms",
                    polled ? "ok" : "FAILED", (unsigned long long)(now - g_wait_input_start_ms));
                Note_Script_Result(std::string("wait_input ") + (polled ? "ok " : "FAILED ") +
                                   std::to_string(now - g_wait_input_start_ms) + " ms");
                g_wait_input_until = 0;
            }
            if (g_presented_frames < g_wait_frames_until) {
                return;
            }
            while (g_script_position < g_script.size() && now >= g_script_resume_ms && g_expect_lbx.empty() &&
                   g_presented_frames >= g_wait_frames_until && g_wait_input_until == 0) {
                const s_script_step& step = g_script[g_script_position++];
                if (step.command == "click" || step.command == "rclick" || step.command == "key" || step.command == "wheel") {
                    g_last_input_serial = antares::lbx_trace::Serial();
                }
                if (step.command == "wait_input") {
                    g_wait_input_count = (uint64_t)std::max(step.a, 1);
                    g_wait_input_until = antares::input_trace::Polls() + g_wait_input_count;
                    g_wait_input_lbx_serial = antares::lbx_trace::Serial();
                    g_wait_input_layout = antares::input_trace::Layout();
                    g_wait_input_start_ms = now;
                    g_wait_input_deadline_ms = now + (uint64_t)step.b;
                } else if (step.command == "wait_frames") {
                    g_wait_frames_until = g_presented_frames + (uint64_t)step.a;
                    Request_Present_Refresh();
                } else if (step.command == "expect_screen") {
                    g_expect_lbx = step.text;
                    g_expect_start_ms = now;
                    g_expect_deadline_ms = now + (uint64_t)step.a;
                } else if (step.command == "screenshot_hd") {
                    g_pending_screenshot_hd = step.text;
                    g_hd_frame_valid = false; // resolve the whole frame, not only changed rows
                    Request_Present_Refresh();
                } else if (step.command == "screenshot_page") {
                    g_pending_screenshot_page = step.text;
                    Request_Present_Refresh();
                } else if (step.command == "log") {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: log %s", step.text.c_str());
                } else if (step.command == "wait") {
                    g_script_resume_ms = now + (uint64_t)step.a;
                } else if (step.command == "move" || step.command == "click" || step.command == "rclick") {
                    Set_Present_Mouse_Position((int16_t)step.a, (int16_t)step.b);
                    Forward_Game_Mouse_State((int16_t)step.a, (int16_t)step.b, 0, false);
                    if (step.command != "move") {
                        Forward_Game_Mouse_State((int16_t)step.a, (int16_t)step.b, step.command == "click" ? 1 : 2, false);
                        // Release on a later pass so the game sees the press.
                        g_script.insert(g_script.begin() + (ptrdiff_t)g_script_position,
                            {s_script_step{"release", step.a, step.b, {}}});
                        g_script.insert(g_script.begin() + (ptrdiff_t)g_script_position, {s_script_step{"wait", 120, 0, {}}});
                    }
                } else if (step.command == "release") {
                    Forward_Game_Mouse_State((int16_t)step.a, (int16_t)step.b, 0, false);
                } else if (step.command == "wheel") {
                    SDL_Event event = {};
                    event.type = SDL_EVENT_MOUSE_WHEEL;
                    event.wheel.y = (float)step.a;
                    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
                    (void)SDL_PushEvent(&event);
                } else if (step.command == "key") {
                    SDL_Event event = {};
                    event.type = SDL_EVENT_KEY_DOWN;
                    event.key.key = SDL_GetKeyFromName(step.text.c_str());
                    event.key.scancode = SDL_GetScancodeFromKey(event.key.key, nullptr);
                    event.key.down = true;
                    (void)SDL_PushEvent(&event);
                    event.type = SDL_EVENT_KEY_UP;
                    event.key.down = false;
                    (void)SDL_PushEvent(&event);
                } else if (step.command == "event") {
                    // Window events: focus_lost, focus_gained, exposed.
                    SDL_Event event = {};
                    event.type = step.text == "focus_lost"     ? SDL_EVENT_WINDOW_FOCUS_LOST
                                 : step.text == "focus_gained" ? SDL_EVENT_WINDOW_FOCUS_GAINED
                                                               : SDL_EVENT_WINDOW_EXPOSED;
                    event.window.windowID = g_sdl_window != nullptr ? SDL_GetWindowID(g_sdl_window) : 0;
                    (void)SDL_PushEvent(&event);
                } else if (step.command == "screenshot") {
                    g_pending_screenshot = step.text;
                    Request_Present_Refresh();
                } else if (step.command == "reply") {
                    // Answer once the screenshots this command asked for are on disk.
                    if (!g_pending_screenshot_hd.empty() || !g_pending_screenshot_page.empty() || !g_pending_screenshot.empty()) {
                        --g_script_position;
                        break;
                    }
                    Send_Control_Reply();
                } else if (step.command == "quit") {
                    Note_Script_Result("quit");
                    Send_Control_Reply();
                    Request_SDL_Exit();
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "input script: unknown command '%s'", step.command.c_str());
                }
            }
        }

        void Save_Pending_Screenshot() {
            if (g_pending_screenshot.empty() || g_sdl_renderer == nullptr) {
                return;
            }
            SDL_Surface* shot = SDL_RenderReadPixels(g_sdl_renderer, nullptr);
            if (shot != nullptr) {
                (void)SDL_SaveBMP(shot, g_pending_screenshot.c_str());
                SDL_DestroySurface(shot);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: screenshot %s", g_pending_screenshot.c_str());
            }
            g_pending_screenshot.clear();
        }
    } // namespace

    void __cdecl Service_Pending_Window_Events() {
        Run_Input_Script();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (antares::ui::Handle_Event(event, g_sdl_window)) {
                Request_Present_Refresh();
                continue;
            }
            if (event.type == SDL_EVENT_QUIT ||
                event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                Request_SDL_Exit();
                continue;
            }

            if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
                event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_RESTORED ||
                event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_RENDER_TARGETS_RESET ||
                event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
                g_hd_frame_valid = false;
                Queue_Debug_Overlay_Display_Size();
                Request_Present_Refresh();
                continue;
            }

            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                g_window_focus_state = 0;
                g_sdl_mouse_buttons = 0;
                g_game_mouse_buttons = 0;
                Enqueue_Mouse_Input_Event(g_mouse_x, g_mouse_y, 0);
                debug_overlay::Handle_Focus(false);
                continue;
            }

            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                g_window_focus_state = 1;
                debug_overlay::Handle_Focus(true);
                Request_Present_Refresh();
                Sync_Mouse_State_From_SDL();
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                const bool overlay_captures_keyboard =
                    debug_overlay::Handle_Key_Event(event.key, g_sdl_window);
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F10) {
                    Request_Present_Refresh();
                    Sync_Mouse_State_From_SDL();
                }
                if (event.type == SDL_EVENT_KEY_DOWN && !overlay_captures_keyboard) {
                    Handle_SDL_Key_Down(event.key);
                }
                continue;
            }

            if (event.type == SDL_EVENT_TEXT_INPUT) {
                (void)debug_overlay::Handle_Text_Input(event.text.text);
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                int16_t mouse_x = 0;
                int16_t mouse_y = 0;
                Map_Window_Point_To_Game_Point(event.motion.x, event.motion.y, &mouse_x, &mouse_y);
                int32_t raw_width = 0;
                int32_t raw_height = 0;
                Get_Raw_Screen_Resolution(&raw_width, &raw_height);

                Set_Present_Mouse_Position(mouse_x, mouse_y);
                const bool overlay_captures_mouse = debug_overlay::Handle_Mouse_Position(
                    mouse_x,
                    mouse_y,
                    event.motion.x,
                    event.motion.y,
                    raw_width,
                    raw_height);
                Forward_Game_Mouse_State(mouse_x, mouse_y, g_sdl_mouse_buttons, overlay_captures_mouse);
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                uint16_t mouse_buttons = g_sdl_mouse_buttons;

                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (event.button.down) {
                        mouse_buttons |= 1;
                    } else {
                        mouse_buttons &= (uint16_t)~1u;
                    }
                }

                if (event.button.button == SDL_BUTTON_RIGHT) {
                    if (event.button.down) {
                        mouse_buttons |= 2;
                    } else {
                        mouse_buttons &= (uint16_t)~2u;
                    }
                }

                g_sdl_mouse_buttons = mouse_buttons;

                int16_t mouse_x = 0;
                int16_t mouse_y = 0;
                Map_Window_Point_To_Game_Point(event.button.x, event.button.y, &mouse_x, &mouse_y);
                int32_t raw_width = 0;
                int32_t raw_height = 0;
                Get_Raw_Screen_Resolution(&raw_width, &raw_height);

                Set_Present_Mouse_Position(mouse_x, mouse_y);
                const bool overlay_captures_mouse = debug_overlay::Handle_Mouse_Button(
                    event.button.button,
                    event.button.down,
                    mouse_x,
                    mouse_y,
                    event.button.x,
                    event.button.y,
                    raw_width,
                    raw_height);
                Forward_Game_Mouse_State(mouse_x, mouse_y, mouse_buttons, overlay_captures_mouse);
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                float wheel_x = event.wheel.x;
                float wheel_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheel_x = -wheel_x;
                    wheel_y = -wheel_y;
                }
                if (!debug_overlay::Handle_Mouse_Wheel(wheel_x, wheel_y)) {
                    mouse_wheel::Add_Steps(wheel_y);
                }
                continue;
            }
        }

        Sync_Debug_Overlay_Cursor();

        if (g_window_focus_state == 0) {
            g_has_lost_focus = true;
        }

        if (g_has_lost_focus && g_window_focus_state == 1) {
            g_has_lost_focus = false;
            Restore_Surfaces();
        }
    }

    int32_t __cdecl Lock_Offscreen_Surface() {
        platform::offscreen_surface = video::g_page_back.pixels;
        return video::g_screen_res_x;
    }

    void __cdecl Unlock_Offscreen_Surface() {
    }

    uint32_t Lock_Appropriate_Render_Buffer() {
        video::current_video_page = video::Get_Selected_Page_Pixels();
        return (uint32_t)video::g_screen_res_x;
    }

    void Unlock_Appropriate_Render_Buffer() {
    }

    void Copy_Off_Page_To_Present_Surface() {
        uint8_t* dst = g_present_surface;
        const uint8_t* src = video::g_page_published.pixels;
        memcpy(g_present_palette_entries, video::g_submitted_palette_entries, sizeof(g_present_palette_entries));
        for (int32_t y = 0; y < video::g_screen_res_y; ++y) {
            memcpy(dst + y * video::g_screen_res_x, src + y * video::g_screen_res_x, (size_t)video::g_screen_res_x);
        }
        render::Snapshot_Plane();
        render::Parity_Check(g_present_surface, g_present_palette_entries);
    }

    void Force_Cursor_Pos(int16_t x, int16_t y) {
        Set_Logical_Mouse_Position(x, y);
        Set_Present_Mouse_Position(x, y);

        if (g_sdl_window != nullptr) {
            float window_x = 0.0f;
            float window_y = 0.0f;
            Map_Game_Point_To_Window_Point(x, y, &window_x, &window_y);
            SDL_WarpMouseInWindow(
                g_sdl_window,
                window_x,
                window_y);
        }
    }

    int16_t __cdecl Get_Mouse_X() {
        return g_mouse_x;
    }

    int16_t __cdecl Get_Mouse_Y() {
        return g_mouse_y;
    }

    void __cdecl Set_Logical_Mouse_Position(int16_t x, int16_t y) {
        g_mouse_x = x;
        g_mouse_y = y;
        Set_Present_Mouse_Position(x, y);
    }

    void __cdecl Create_Cursor_Surface() {
        if (platform::g_cursor_surface_1 == nullptr) {
            platform::g_cursor_surface_1 = (uint8_t*)malloc(k_cursor_pixel_count);
        }

        if (platform::cursor_surface == nullptr) {
            platform::cursor_surface = (uint8_t*)malloc(k_cursor_pixel_count);
        }

        if (platform::g_cursor_surface_1 == nullptr || platform::cursor_surface == nullptr) {
            exitm::Exit_With_Message("Create_Cursor_Surface failed");
        }

        memset(platform::g_cursor_surface_1, 0, k_cursor_pixel_count);
        memset(platform::cursor_surface, 0, k_cursor_pixel_count);

        if (g_present_cursor_mutex != nullptr) {
            SDL_LockMutex(g_present_cursor_mutex);
        }

        g_pending_cursor_state.pixels = platform::cursor_surface;
        g_pending_cursor_state.offset = 0;
        g_pending_cursor_state.shape = 0;
        g_pending_cursor_state.enabled = 0;
        g_active_cursor_state.pixels = platform::g_cursor_surface_1;
        g_active_cursor_state.offset = 0;
        g_active_cursor_state.shape = 0;
        g_active_cursor_state.enabled = 0;
        g_pending_cursor_generation = 0;
        g_applied_cursor_generation = 0;

        if (g_present_cursor_mutex != nullptr) {
            SDL_UnlockMutex(g_present_cursor_mutex);
        }
    }

    void __cdecl Toggle_Pages_Stub() {
        video::Toggle_Pages();
    }

    int Run_Main_Loop(const char* command_line) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: Run_Main_Loop start cmd='%s'", command_line != nullptr ? command_line : "");

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
            return 0;
        }
        config::Register_Commit_Listener(Publish_Present_Scaler_Config);
        Publish_Present_Scaler_Config();
        const char* video_driver = SDL_GetCurrentVideoDriver();
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "platform: SDL video initialized, driver: %s",
            video_driver != nullptr ? video_driver : "<unknown>");
        if (!Init_Exit()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: exit primitives initialization failed: %s", SDL_GetError());
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }

        g_pending_toggle_pages = false;
        g_window_focus_state = 1;
        g_has_lost_focus = false;
        g_input_queue_read = 0;
        g_input_queue_write = 0;
        g_sdl_mouse_buttons = 0;
        g_game_mouse_buttons = 0;
        g_game_mouse_suppressed = false;
        g_present_generation = 0;
        SDL_SetAtomicInt(&g_present_refresh_requested, 0);
        Set_Present_Mouse_Position(0, 0);
        g_pending_cursor_state = {};
        g_active_cursor_state = {};
        g_pending_cursor_generation = 0;
        g_applied_cursor_generation = 0;

        if (g_input_queue_mutex == nullptr) {
            g_input_queue_mutex = SDL_CreateMutex();
        }
        if (g_input_queue_mutex == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_CreateMutex failed: %s", SDL_GetError());
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }

        if (g_present_cursor_mutex == nullptr) {
            g_present_cursor_mutex = SDL_CreateMutex();
        }
        if (g_present_cursor_mutex == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_CreateMutex failed: %s", SDL_GetError());
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }

        SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
        if (g_fullscreen != 0) {
            window_flags |= SDL_WINDOW_FULLSCREEN;
        }

        g_sdl_window = SDL_CreateWindow(antares::PRODUCT_NAME, g_window_width, g_window_height, window_flags);
        if (g_sdl_window == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_DestroyMutex(g_present_cursor_mutex);
            g_present_cursor_mutex = nullptr;
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: hidden window created");

        g_main_window = g_sdl_window;

        g_sdl_renderer = SDL_CreateRenderer(g_sdl_window, nullptr);
        if (g_sdl_renderer == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_CreateRenderer failed: %s", SDL_GetError());
            Destroy_Main_Window();
            SDL_DestroyMutex(g_present_cursor_mutex);
            g_present_cursor_mutex = nullptr;
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }
        g_applied_vsync_enabled = -1;
        SDL_ShowWindow(g_sdl_window);
        const char* renderer_name = SDL_GetRendererName(g_sdl_renderer);
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "platform: renderer initialized and window shown, backend: %s",
            renderer_name != nullptr ? renderer_name : "<unknown>");

        SDL_HideCursor();

        if (!Create_Video_Buffers(4)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: Create_Video_Buffers failed");
            Destroy_Main_Window();
            SDL_DestroyMutex(g_present_cursor_mutex);
            g_present_cursor_mutex = nullptr;
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }
        if (!Ensure_Render_Presentation()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_SetRenderLogicalPresentation failed: %s", SDL_GetError());
            Cleanup_Surfaces();
            Destroy_Main_Window();
            SDL_DestroyMutex(g_present_cursor_mutex);
            g_present_cursor_mutex = nullptr;
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: video buffers created");
        if (!debug_overlay::Initialize(g_sdl_renderer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: debug overlay initialization failed: %s", SDL_GetError());
            Cleanup_Surfaces();
            Destroy_Main_Window();
            SDL_DestroyMutex(g_present_cursor_mutex);
            g_present_cursor_mutex = nullptr;
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }
        Queue_Debug_Overlay_Display_Size();

        g_worker_thread_handle = SDL_CreateThread(MOX2::thunk_main2, "MOX2::main2", nullptr);
        if (g_worker_thread_handle == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform: SDL_CreateThread failed: %s", SDL_GetError());
            debug_overlay::Shutdown();
            Cleanup_Surfaces();
            Destroy_Main_Window();
            SDL_DestroyMutex(g_present_cursor_mutex);
            g_present_cursor_mutex = nullptr;
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
            Shutdown_Exit();
            SDL_Quit();
            return 0;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: worker thread created");

        while (!Game_Thread_Exited()) {
            Service_Pending_Window_Events();

            Present_Published_Off_Page();
            SDL_Delay(1);
        }

        if (!Exit_Requested()) {
            Request_Exit();
        }

        if (g_worker_thread_handle != nullptr) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: waiting for worker thread");
            SDL_WaitThread(g_worker_thread_handle, nullptr);
            g_worker_thread_handle = nullptr;
        }

        const int exit_status = Exit_Status();
        exitm::Present_Exit_Message(g_main_window);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "platform: Run_Main_Loop shutdown");
        debug_overlay::Shutdown();
        Cleanup_Surfaces();
        Destroy_Main_Window();
        if (g_input_queue_mutex != nullptr) {
            SDL_DestroyMutex(g_input_queue_mutex);
            g_input_queue_mutex = nullptr;
        }
        if (g_present_cursor_mutex != nullptr) {
            SDL_DestroyMutex(g_present_cursor_mutex);
            g_present_cursor_mutex = nullptr;
        }
        Shutdown_Exit();
        SDL_Quit();

        return exit_status;
    }

    void __cdecl Pump_Game_Input_Queue() {
        if (g_input_queue_mutex == nullptr) {
            return;
        }

        s_input_event pending_events[k_input_queue_capacity];
        uint16_t pending_count = 0;

        SDL_LockMutex(g_input_queue_mutex);
        while (g_input_queue_read != g_input_queue_write && pending_count < k_input_queue_capacity) {
            pending_events[pending_count] = g_input_event_queue[g_input_queue_read];
            g_input_queue_read = (uint16_t)((g_input_queue_read + 1) % k_input_queue_capacity);
            ++pending_count;
        }
        SDL_UnlockMutex(g_input_queue_mutex);

        for (uint16_t i = 0; i < pending_count; ++i) {
            const s_input_event& event = pending_events[i];
            if (event.type == INPUT_EVENT_MOUSE) {
                mouse::User_Mouse_Handler(event.mouse_x, event.mouse_y, event.mouse_buttons);
            } else {
                Queue_Key_Code(event.key_code, event.scan_code);
            }
        }
    }

    void __cdecl Update_Cursor_Image() {
        if (cursor_surface == nullptr || g_present_cursor_mutex == nullptr) {
            return;
        }

        const int16_t cursor_offset = (int16_t)mouse::Get_Pointer_Offset();
        const int16_t cursor_shape = (int16_t)mouse::Get_Pointer_Image_Number();
        const uint8_t cursor_enabled = (mouse::Mouse_Enabled() != 0) ? 1 : 0;
        int16_t published_cursor_shape = 0;

        SDL_LockMutex(g_present_cursor_mutex);
        s_present_cursor_state& cursor_state = g_pending_cursor_state;

        cursor_state.pixels = cursor_surface;
        cursor_state.offset = cursor_offset;
        cursor_state.shape = cursor_shape;
        cursor_state.enabled = cursor_enabled;

        if (cursor_state.enabled != 0 &&
            cursor_state.shape > 0 &&
            video::mouse_palette != nullptr) {
            uint8_t* const src =
                &video::mouse_palette[((int32_t)cursor_state.shape - 1) * k_cursor_pixel_count];

            for (int32_t row = 0; row < k_cursor_dimension; ++row) {
                memcpy(
                    &cursor_state.pixels[row * k_cursor_dimension],
                    &src[row * k_cursor_dimension],
                    k_cursor_dimension);
            }
        } else {
            memset(cursor_state.pixels, 0, k_cursor_pixel_count);
        }

        published_cursor_shape = cursor_state.shape;
        ++g_pending_cursor_generation;
        SDL_UnlockMutex(g_present_cursor_mutex);

        g_last_cursor_shape = (int32_t)published_cursor_shape;
        Request_Present_Refresh();
    }

    void __cdecl Copy_Dwords(uint32_t* destination, const uint32_t* source, uint32_t dword_count) {
        memcpy(destination, source, dword_count * sizeof(uint32_t));
    }

} // namespace platform

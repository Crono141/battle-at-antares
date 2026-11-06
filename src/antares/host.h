#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Frontend host (WP3.0, PLAN.md section 4 "Phase 3"): lets another program own the window and
// show the game. The host runs the game's own startup and platform loop on a thread of its choice,
// with SDL's dummy video driver, so the game thread, sound, timers and files work as they do in
// the SDL build. The host then:
// - takes each resolved 4x frame (Frame_Resolved, called by the presenter instead of uploading
//   it to the SDL window);
// - sends input as SDL events in 640x480 game coordinates, so the game's own input path
//   (hotkeys, wheel, modifiers) handles it;
// - reads a game-state snapshot that the game thread publishes with each frame (threading rule:
//   the host never reads rules state directly).
// Nothing here runs unless a host calls Activate() before the platform loop starts.
namespace antares::host {

    // ---- Host side (any thread) ----

    /** Marks the process as hosted. Call before the game starts. */
    void Activate();
    bool Active();

    // The host starts the game itself, by calling the game's main() on its own thread (the Godot
    // host compiles src/main.cpp in under another name).
    /** Asks the game to quit (the platform loop ends after the game thread). */
    void Request_Quit();
    bool Game_Running();
    /** The profile's window settings (valid once the game has loaded its configuration). */
    void Window_Settings(int32_t* width, int32_t* height, bool* fullscreen);

    /** Copies the newest frame into `argb` (width*height ARGB8888, resized as needed) when one
     *  arrived since `*generation`; [row_first, row_last) are the rows that changed. */
    /** The generation of the newest published frame (Take_Frame hands out the same numbers). */
    uint64_t Frame_Generation();
    bool Take_Frame(std::vector<uint32_t>* argb, int32_t* width, int32_t* height, uint64_t* generation,
        int32_t* row_first, int32_t* row_last);

    /** The game's cursor image, `size` x `size` ARGB8888 (24, or 96 with HD art; 0 = hidden),
     *  drawn with its top-left corner at the pointer, 24 game pixels wide. Copies it into `argb`
     *  when it changed since `*generation`. */
    bool Take_Cursor(std::vector<uint32_t>* argb, int32_t* size, uint64_t* generation);

    // Input in game coordinates (0-639, 0-479; fractions allowed).
    void Send_Mouse_Motion(float game_x, float game_y);
    /** button: 1 left, 2 right, 3 middle. */
    void Send_Mouse_Button(float game_x, float game_y, int32_t button, bool down);
    void Send_Wheel(float game_x, float game_y, float steps);
    /** `keycode` is an SDL keycode (SDLK_*), `modifiers` SDL_Keymod bits. */
    void Send_Key(uint32_t keycode, uint16_t modifiers, bool down);

    struct s_star {
        std::string name;
        int16_t x = 0;
        int16_t y = 0;
        int8_t size = 0;
        int8_t spectral_class = 0;
        int8_t owner = -1;     // player index, or -1
        uint8_t owner_color = 0;
        std::string owner_name; // the owner's race name
        bool in_nebula = false;
        int8_t special = 0;
    };
    struct s_galaxy {
        uint64_t generation = 0; // changes when the snapshot does
        bool valid = false;      // a game is loaded
        std::vector<s_star> stars;
    };
    /** The newest published snapshot. */
    s_galaxy Galaxy();

    // ---- Game side (hooks) ----

    /** Presenter, after the HD frame is resolved. True when hosted: the frame was taken and the
     *  SDL upload is skipped. */
    bool Frame_Resolved(const uint32_t* argb, int32_t width, int32_t height, bool changed, int32_t row_first,
        int32_t row_last);
    /** Presenter, with each hosted frame: the current cursor image (size 0 = hidden). */
    void Cursor_Image(const uint32_t* argb, int32_t size);
    /** Game thread, in video::Publish_Off_Page: refreshes the game-state snapshot. */
    void On_Publish();

} // namespace antares::host

#pragma once

#include <SDL3/SDL.h>

// WP3.0 trial path B: a UI layer (RmlUi plus a native galaxy scene view) inside the SDL platform
// loop, hooked where the debug overlay is. Built only with ANTARES_BUILD_RMLUI
// (frontend/rmlui); otherwise these are no-ops. Platform thread only.
namespace antares::ui {
    /** Every window event, before the game sees it. True: the UI took it (the platform then
     *  skips the event and presents again). */
    bool Handle_Event(SDL_Event& event, SDL_Window* window);

    /** After the game's frame is drawn, before it is presented. True: the UI is animating and
     *  wants another present. */
    bool Render(SDL_Renderer* renderer);
} // namespace antares::ui

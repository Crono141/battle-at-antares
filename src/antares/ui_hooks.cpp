#include "pch.h"

#include "antares/ui_hooks.h"

#if !defined(ANTARES_RMLUI)
namespace antares::ui {
    bool Handle_Event(SDL_Event&, SDL_Window*) {
        return false;
    }

    bool Render(SDL_Renderer*) {
        return false;
    }
} // namespace antares::ui
#endif

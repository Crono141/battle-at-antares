#pragma once

// Spike 3.9 (notes/antares/combat-capture-spike.md): the legacy drawing code paints one effect at a
// time onto the hidden off page, cleared to transparent, and each frame is read out as an RGBA
// image at the HD scale, so a native frontend can play the original's effects as sprites under
// its own camera.
//
// Glass (shields, stasis, cloak: the game recolours what lies underneath through a 256-entry
// table) can't be an alpha image: glass over nothing becomes a mark per pixel, naming the table
// it used, and the frontend applies that table over its own picture (Glass_Lut gives it as a
// colour lookup).
//
// Game thread only.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace render::capture {

    struct s_frame {
        int32_t x = 0; // the crop's top-left on the page, in 1x pixels
        int32_t y = 0;
        int32_t width = 0; // in plane pixels (1x times the HD scale)
        int32_t height = 0;
        std::vector<uint8_t> rgba;  // straight alpha
        std::vector<uint8_t> glass; // per pixel: 0 none, else the table's id + 1
    };

    // True when the HD planes exist (an HD pack is in use); capture needs them.
    bool Available();
    // The HD scale (plane pixels per 1x pixel).
    int32_t Scale();

    // Starts a capture of the page window [0, width) x [0, height) (1x): its plane and 1x pixels are
    // saved, to be restored by End.
    void Begin(int32_t width, int32_t height);
    // Clears the window to transparent (before each frame's drawing).
    void Clear();
    // Reads the window: false when nothing was painted. Cropped to what was painted.
    bool Frame(s_frame* out);
    void End();
    bool Active();

    // The glass tables marks name (by id), and each as a 32 x 32 x 32 colour lookup (RGB8, index
    // r + g * 32 + b * 1024): a colour through the table as the HD renderer does it.
    const std::vector<std::array<uint8_t, 256>>& Tables();
    std::vector<uint8_t> Glass_Lut(int32_t table_id);

    // The drawing operations run and the Unported fallbacks since Begin ("op=count,...").
    std::string Ops();
    std::string Unported();

    // The page window resolved (straight RGB, alpha 255) as the presenter would show it: for the
    // spike's comparison against a captured frame laid over the effect-free background.
    void Resolve_Window(std::vector<uint8_t>* rgba, int32_t* width, int32_t* height);
    // `frame` laid over `background` (both window-sized RGBA after Resolve_Window), glass marks
    // applied through their tables: what the frontend should show.
    void Composite(const s_frame& frame, std::vector<uint8_t>* background, int32_t width);

} // namespace render::capture

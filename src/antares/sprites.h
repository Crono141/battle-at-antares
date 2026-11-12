#pragma once

// WP3.3a: the game's art as images for a native frontend. A native view asks for an LBX entry
// ("BUFFER0.LBX", 148: a star) and gets its frames as RGBA, from the HD pack where it has art
// (render::Export_Art) and otherwise from the original 1x pixels. Indexed pixels take the colours
// of the game's current palette, overlaid by the entry's own palette when it carries one (the
// planet pictures), so an entry shows as it does in the legacy screen that loads it.
//
// The entry is read into a private buffer, never the game's sprite caches, so exporting art
// can't move or evict what a legacy screen holds. Game thread only (VFS, palette, HD cache).

#include <cstdint>
#include <string>
#include <vector>

namespace antares::sprites {

    struct s_frame {
        int32_t width = 0; // in image pixels (the 1x size times `scale`, or the art's own size)
        int32_t height = 0;
        int32_t origin_x = 0; // top-left in 1x pixels from the 1x frame's top-left
        int32_t origin_y = 0;
        std::vector<uint8_t> rgba;
    };

    struct s_sprite {
        std::string lbx;
        int32_t entry = 0;
        int32_t width = 0; // the 1x frame size
        int32_t height = 0;
        int32_t scale = 1; // image pixels per 1x pixel: 4 with HD art, 1 without
        bool hd = false;
        std::vector<s_frame> frames;
        std::string error; // empty when it loaded
    };

    bool Export(const std::string& lbx, int32_t entry, s_sprite* out);

    // The palette Export uses, 0xRRGGBB per index (exposed for tests): the current palette, then
    // the entry's own entries when it has one.
    void Palette_For(const uint8_t* entry_bytes, size_t size, uint32_t palette_rgb[256]);
    // An entry's own embedded palette (its range) laid over `palette_rgb`.
    void Apply_Embedded_Palette(const uint8_t* entry_bytes, size_t size, uint32_t palette_rgb[256]);

} // namespace antares::sprites

#pragma once

// Art pack format v2 (PLAN.md section 5, WP1.7): what can be told about an art file without the
// engine. Pure functions, unit tested (tests/unit/art_format_test.cpp).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace render::art {

    // The PNG colour type from the IHDR chunk (0 grey, 2 RGB, 3 palette, 4 grey + alpha,
    // 6 RGBA), or -1 when the bytes are not a PNG.
    int32_t Png_Colour_Type(const uint8_t* bytes, size_t size);

    // Whether a file holds full-colour art. Grey PNGs (with or without alpha) are our indexed
    // format: grey value = palette index. RGB, RGBA and paletted PNGs are colours (palettes from
    // image editors are reordered on save, so their numbers are never trusted), WebP likewise.
    bool Is_Full_Colour(const uint8_t* bytes, size_t size);

    // The scale of art `image_width` x `image_height` drawn over a 1x frame of `base_width` x
    // `base_height`: the exact ratio, the same both ways, and 1, 2 or 4 (3 for full colour only,
    // D8). 0 when it is none of those.
    int32_t Infer_Scale(int32_t image_width, int32_t image_height, int32_t base_width, int32_t base_height, bool full_colour);

    // The size art of `size` pixels at `scale` takes at scale `target` (rounded).
    int32_t Scaled_Size(int32_t size, int32_t scale, int32_t target);

    // Resamples a plane of `channels` bytes per pixel from `width` x `height` to `to_width` x
    // `to_height`. Nearest: indexed art and masks (values are not colours), and integer
    // enlargements of full-colour art (pixel art stays crisp). Bilinear over premultiplied alpha:
    // the other full-colour resizes (3 to 4). `pixels` are RGBA for the bilinear case.
    std::vector<uint8_t> Resample_Nearest(const uint8_t* pixels, int32_t width, int32_t height, int32_t channels, int32_t to_width, int32_t to_height);
    std::vector<uint8_t> Resample_Bilinear_Rgba(const uint8_t* rgba, int32_t width, int32_t height, int32_t to_width, int32_t to_height);

    // Mask images (D20): an 8-bit grey image beside a full-colour frame (NNN.mask.png) saying
    // which pixels take the game's effects. The value names the game's palette index the pixel
    // stands for:
    //   0           none: the art's own colour (whole-screen fades still apply);
    //   1 - 0xEF    shade: the pixel follows that index, so player colours and recolours reach
    //               it; it keeps the art's detail as brightness relative to the index's colour;
    //   0xF0 - 0xFF glass of level value - 0xF0 when the sprite is glassed (tints what is
    //               under it, as the game's glass does); a shade of that index otherwise.
    enum e_mask_kind : uint8_t {
        MASK_NONE,
        MASK_SHADE,
        MASK_GLASS,
    };
    struct s_mask_meaning {
        e_mask_kind kind;
        uint8_t value; // the index (shade) or the level 0-15 (glass)
    };
    s_mask_meaning Mask_Meaning(uint8_t value, bool glassed);

    // Brightness of an RGB colour (0..255), as the twin planes weigh it.
    inline uint32_t Luma(uint8_t r, uint8_t g, uint8_t b) {
        return ((uint32_t)r * 77u + (uint32_t)g * 150u + (uint32_t)b * 29u) >> 8;
    }

    // WP1.10: full-colour art whose every drawn pixel is masked, with one colour per mask value,
    // is indexed art written in the one format (the tools convert L+A art so: colour = the index's
    // colour, mask = the index). Writes the indices (the mask, 0 where alpha is 0) to `index`,
    // which may be `mask` itself, and returns true; false (and `index` partly written) otherwise.
    bool Collapse_To_Index(const uint8_t* rgb, const uint8_t* alpha, const uint8_t* mask, size_t count, uint8_t* index);

    // The reference brightness of each mask value (1..255) in a frame: the alpha-weighted mean
    // Luma of its pixels masked so; 0 for values not used. A masked pixel shows its index's colour
    // at its own brightness relative to this (KIND_SHADE).
    void Shade_Reference(const uint8_t* rgb, const uint8_t* alpha, const uint8_t* mask, size_t count, uint8_t reference[256]);

} // namespace render::art

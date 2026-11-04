#include "pch.h"

#include "antares/render/art_format.h"
#include "doctest/doctest.h"

#include <vector>

namespace {

    // The first 26 bytes of a PNG with the given colour type (all the inference reads).
    std::vector<uint8_t> Png_Header(uint8_t colour_type) {
        std::vector<uint8_t> bytes = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 8, 0, 0, 0, 4, 8, colour_type};
        return bytes;
    }

} // namespace

using namespace render::art;

DOCTEST_TEST_CASE("art format: grey PNGs are indexed, colour PNGs and WebP are full colour") {
    for (uint8_t type : {0, 4}) {
        const std::vector<uint8_t> png = Png_Header(type);
        DOCTEST_CHECK(Png_Colour_Type(png.data(), png.size()) == type);
        DOCTEST_CHECK_FALSE(Is_Full_Colour(png.data(), png.size()));
    }
    // RGB, paletted (matched by colour, never by palette number) and RGBA.
    for (uint8_t type : {2, 3, 6}) {
        const std::vector<uint8_t> png = Png_Header(type);
        DOCTEST_CHECK(Is_Full_Colour(png.data(), png.size()));
    }
    const uint8_t webp[16] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', ' '};
    DOCTEST_CHECK(Png_Colour_Type(webp, sizeof(webp)) == -1);
    DOCTEST_CHECK(Is_Full_Colour(webp, sizeof(webp)));
    const std::vector<uint8_t> full = Png_Header(4);
    const std::vector<uint8_t> short_png(full.begin(), full.begin() + 20);
    DOCTEST_CHECK(Png_Colour_Type(short_png.data(), short_png.size()) == -1);
}

DOCTEST_TEST_CASE("art format: the scale is the exact ratio to the 1x frame") {
    // Indexed art: 1, 2 and 4.
    DOCTEST_CHECK(Infer_Scale(30, 20, 30, 20, false) == 1);
    DOCTEST_CHECK(Infer_Scale(60, 40, 30, 20, false) == 2);
    DOCTEST_CHECK(Infer_Scale(120, 80, 30, 20, false) == 4);
    DOCTEST_CHECK(Infer_Scale(90, 60, 30, 20, false) == 0); // 3 is full colour only (D8)
    DOCTEST_CHECK(Infer_Scale(90, 60, 30, 20, true) == 3);
    DOCTEST_CHECK(Infer_Scale(150, 100, 30, 20, true) == 0); // 5
    DOCTEST_CHECK(Infer_Scale(240, 160, 30, 20, true) == 0); // 8
    // Not the same both ways, or not a whole ratio: padded art needs a sprite.json.
    DOCTEST_CHECK(Infer_Scale(120, 40, 30, 20, false) == 0);
    DOCTEST_CHECK(Infer_Scale(124, 80, 30, 20, false) == 0);
    DOCTEST_CHECK(Infer_Scale(120, 80, 0, 20, false) == 0);
}

DOCTEST_TEST_CASE("art format: scaled sizes round to the target scale") {
    DOCTEST_CHECK(Scaled_Size(60, 2, 4) == 120);
    DOCTEST_CHECK(Scaled_Size(30, 1, 4) == 120);
    DOCTEST_CHECK(Scaled_Size(90, 3, 4) == 120);
    DOCTEST_CHECK(Scaled_Size(91, 3, 4) == 121);
    DOCTEST_CHECK(Scaled_Size(120, 4, 4) == 120);
}

DOCTEST_TEST_CASE("art format: nearest resampling doubles pixels exactly") {
    const uint8_t pixels[4] = {1, 2, 3, 4}; // 2 x 2, one channel
    const std::vector<uint8_t> out = Resample_Nearest(pixels, 2, 2, 1, 4, 4);
    const std::vector<uint8_t> expected = {1, 1, 2, 2, 1, 1, 2, 2, 3, 3, 4, 4, 3, 3, 4, 4};
    DOCTEST_CHECK(out == expected);
    const uint8_t rgba[8] = {10, 20, 30, 255, 40, 50, 60, 0};
    const std::vector<uint8_t> wide = Resample_Nearest(rgba, 2, 1, 4, 4, 1);
    DOCTEST_CHECK(wide[0] == 10);
    DOCTEST_CHECK(wide[4] == 10);
    DOCTEST_CHECK(wide[8] == 40);
    DOCTEST_CHECK(wide[15] == 0);
}

DOCTEST_TEST_CASE("art format: bilinear resampling keeps colour under transparent neighbours") {
    // Opaque red beside fully transparent green: premultiplied blending keeps the edge red.
    const uint8_t rgba[8] = {255, 0, 0, 255, 0, 255, 0, 0};
    const std::vector<uint8_t> out = Resample_Bilinear_Rgba(rgba, 2, 1, 3, 1);
    DOCTEST_REQUIRE(out.size() == 12);
    DOCTEST_CHECK(out[0] == 255);
    DOCTEST_CHECK(out[3] == 255);
    DOCTEST_CHECK(out[4] == 255); // the middle pixel is half covered, still red
    DOCTEST_CHECK(out[5] == 0);
    DOCTEST_CHECK(out[7] > 0);
    DOCTEST_CHECK(out[7] < 255);
    DOCTEST_CHECK(out[11] == 0);
}

DOCTEST_TEST_CASE("art format: mask values") {
    DOCTEST_CHECK(Mask_Meaning(0, true).kind == MASK_NONE);
    DOCTEST_CHECK(Mask_Meaning(40, false).kind == MASK_SHADE);
    DOCTEST_CHECK(Mask_Meaning(40, false).value == 40);
    DOCTEST_CHECK(Mask_Meaning(0xF3, true).kind == MASK_GLASS);
    DOCTEST_CHECK(Mask_Meaning(0xF3, true).value == 3);
    // Not glassed: a shade of that index.
    DOCTEST_CHECK(Mask_Meaning(0xF3, false).kind == MASK_SHADE);
    DOCTEST_CHECK(Mask_Meaning(0xF3, false).value == 0xF3);
}

DOCTEST_TEST_CASE("art format: masked art with one colour per mask value is its indices (WP1.10)") {
    // Two drawn pixels of index 5 (same colour), one of index 0xF2, one transparent.
    const uint8_t rgb[] = {10, 20, 30, 10, 20, 30, 200, 200, 200, 99, 99, 99};
    const uint8_t alpha[] = {255, 128, 255, 0};
    const uint8_t mask[] = {5, 5, 0xF2, 7};
    uint8_t index[4] = {1, 1, 1, 1};
    DOCTEST_CHECK(Collapse_To_Index(rgb, alpha, mask, 4, index));
    DOCTEST_CHECK(index[0] == 5);
    DOCTEST_CHECK(index[1] == 5);
    DOCTEST_CHECK(index[2] == 0xF2);
    DOCTEST_CHECK(index[3] == 0); // transparent
    // In place, as the decoder does it.
    uint8_t in_place[] = {5, 5, 0xF2, 7};
    DOCTEST_CHECK(Collapse_To_Index(rgb, alpha, in_place, 4, in_place));
    DOCTEST_CHECK(in_place[3] == 0);
    // A drawn pixel without a mask value keeps its own colour: not indexed.
    const uint8_t unmasked[] = {5, 5, 0, 7};
    DOCTEST_CHECK_FALSE(Collapse_To_Index(rgb, alpha, unmasked, 4, index));
    // Two colours under one mask value: shaded art.
    const uint8_t shaded_rgb[] = {10, 20, 30, 11, 20, 30, 200, 200, 200, 0, 0, 0};
    DOCTEST_CHECK_FALSE(Collapse_To_Index(shaded_rgb, alpha, mask, 4, index));
}

DOCTEST_TEST_CASE("art format: the shade reference is the alpha-weighted mean brightness per mask value") {
    const uint8_t rgb[] = {100, 100, 100, 200, 200, 200, 50, 50, 50, 0, 0, 0};
    const uint8_t alpha[] = {255, 255, 255, 0};
    const uint8_t mask[] = {9, 9, 0, 9};
    uint8_t reference[256];
    Shade_Reference(rgb, alpha, mask, 4, reference);
    DOCTEST_CHECK(reference[9] == Luma(150, 150, 150)); // the mean of 100 and 200
    DOCTEST_CHECK(reference[0] == 0);
    DOCTEST_CHECK(reference[10] == 0);
    // One colour per value: the reference is that colour's brightness, so its ratio is exactly 1.
    const uint8_t flat[] = {10, 80, 200, 10, 80, 200, 0, 0, 0, 0, 0, 0};
    Shade_Reference(flat, alpha, mask, 4, reference);
    DOCTEST_CHECK(reference[9] == Luma(10, 80, 200));
}

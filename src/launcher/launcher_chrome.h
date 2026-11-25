#pragma once

#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>

// The setup wizard's look (WP5.11b, owner 2026-11-24: look B, "matches the default chrome without
// being loud about it"): the Chrome theme's metal, frame, buttons and fonts, embedded in the launcher
// so they exist before any game file does. The launcher's own tabs keep the plain style.
namespace launcher::chrome {
    inline const ImVec4 LIGHT(226 / 255.0f, 222 / 255.0f, 210 / 255.0f, 1.0f);
    inline const ImVec4 GOLD(240 / 255.0f, 214 / 255.0f, 150 / 255.0f, 1.0f);
    inline const ImVec4 DIM(170 / 255.0f, 172 / 255.0f, 180 / 255.0f, 1.0f);
    inline const ImVec4 AMBER(1.0f, 138 / 255.0f, 28 / 255.0f, 1.0f);
    inline const ImVec4 GREEN(140 / 255.0f, 230 / 255.0f, 120 / 255.0f, 1.0f);
    inline const ImVec4 RED(1.0f, 110 / 255.0f, 90 / 255.0f, 1.0f);

    /** Loads the fonts into the atlas and the art into textures. Call once after the ImGui context
     *  and the renderer exist. False when something didn't load (the wizard then draws plainly). */
    /** The look's art and fonts; `data_dir` (the program's data/) is where the theme package is
     *  read from when the art isn't built in. */
    bool Load(SDL_Renderer* renderer, const std::string& data_dir);
    void Unload();
    bool Ready();

    ImFont* Title_Font(); // Russo One
    ImFont* Body_Font();  // Saira Semi Condensed Medium
    ImFont* Bold_Font();  // Saira Semi Condensed Bold

    /** The metal panel over the whole rectangle (scaled to cover, centred). */
    void Background(ImDrawList* draw, ImVec2 min, ImVec2 max);
    /** The bezel frame, nine-sliced, its middle filled. */
    void Bezel(ImDrawList* draw, ImVec2 min, ImVec2 max);
    /** A game-style metal button; `main` glows amber. Returns true when pressed. */
    bool Button(const char* id_and_label, ImVec2 size, bool main, bool enabled = true);
    /** Text with a 2 px black shadow below it, as the theme's headings. */
    void Shadowed_Text(ImDrawList* draw, ImFont* font, float size, ImVec2 at, const ImVec4& colour, const char* text);
    /** A square box with a tick (ok), a cross (!ok) or an exclamation mark (warning). */
    void Check_Box(ImDrawList* draw, ImVec2 min, float size, bool ok, bool warning = false);
} // namespace launcher::chrome

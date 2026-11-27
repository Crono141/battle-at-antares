#include "pch.h"

#include "launcher/launcher_chrome.h"

#include <algorithm>
#include <array>
#include <string>

#include <vector>

#include "miniz.h"
#if defined(ANTARES_LAUNCHER_EMBEDDED_ART)
#include "k_chrome_bezel.h"
#include "k_chrome_button.h"
#include "k_chrome_panel.h"
#endif
#include "k_chrome_russo.h"
#include "k_chrome_saira.h"
#include "k_chrome_saira_bold.h"

namespace launcher::chrome {
    namespace {
        struct s_texture {
            SDL_Texture* texture = nullptr;
            float width = 0.0f;
            float height = 0.0f;
        };
        s_texture s_panel, s_bezel, s_button;
        ImFont* s_russo = nullptr;
        ImFont* s_saira = nullptr;
        ImFont* s_saira_bold = nullptr;
        bool s_ready = false;

        // Slices from the theme's art/pieces.json.
        constexpr float BEZEL_MARGIN = 40.0f; // source pixels
        constexpr float BEZEL_SCALE = 0.5f;
        constexpr float BUTTON_CAP = 150.0f; // source pixels

        bool Load_Texture_(SDL_Renderer* renderer, const unsigned char* data, size_t size, s_texture* out) {
            SDL_IOStream* stream = SDL_IOFromConstMem(data, size);
            SDL_Surface* surface = stream != nullptr ? SDL_LoadPNG_IO(stream, true) : nullptr;
            if (surface == nullptr) return false;
            // SDL's software renderer (headless screenshots, machines without a GPU) draws nothing
            // from a texture this wide: halve it there. Checked 2026-11-24 with the 1220 px panel.
            const char* name = SDL_GetRendererName(renderer);
            if (name != nullptr && SDL_strcmp(name, SDL_SOFTWARE_RENDERER) == 0 && surface->w > 1024) {
                SDL_Surface* half = SDL_ScaleSurface(surface, surface->w / 2, surface->h / 2, SDL_SCALEMODE_LINEAR);
                SDL_DestroySurface(surface);
                surface = half;
            }
            out->texture = SDL_CreateTextureFromSurface(renderer, surface);
            out->width = static_cast<float>(surface->w);
            out->height = static_cast<float>(surface->h);
            SDL_DestroySurface(surface);
            if (out->texture != nullptr) SDL_SetTextureScaleMode(out->texture, SDL_SCALEMODE_LINEAR);
            return out->texture != nullptr;
        }

        ImFont* Load_Font_(const unsigned char* data, size_t size) {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), 17.0f, &config);
        }

        ImTextureRef Ref_(const s_texture& texture) {
            return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture.texture)));
        }

        ImU32 Colour_(const ImVec4& colour, float alpha = 1.0f) {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(colour.x, colour.y, colour.z, colour.w * alpha));
        }

        // One part of a nine- or three-slice: a source rectangle drawn into a target rectangle.
        void Slice_(ImDrawList* draw, const s_texture& texture, ImVec2 min, ImVec2 max, ImVec2 source_min, ImVec2 source_max, ImU32 tint) {
            if (max.x <= min.x || max.y <= min.y) return;
            draw->AddImage(Ref_(texture), min, max, ImVec2(source_min.x / texture.width, source_min.y / texture.height), ImVec2(source_max.x / texture.width, source_max.y / texture.height), tint);
        }
    } // namespace

    namespace {
        // The theme's art from the shipped package (a build without it embedded: the theme art
        // is never committed): data/mods/antares-ui-chrome.zip holds godot/antares-ui-chrome.zip,
        // the Godot pack, which holds packages/antares-ui-chrome/art/*.png.
        bool Package_Art_(const std::string& data_dir, std::vector<std::vector<uint8_t>>* out) {
            const char* names[] = {"panel.png", "bezel.png", "button.png"};
            std::vector<uint8_t> pack;
            for (const std::string& outer : {data_dir + "mods/antares-ui-chrome.zip", std::string()}) {
                if (!outer.empty()) {
                    mz_zip_archive zip{};
                    if (!mz_zip_reader_init_file(&zip, outer.c_str(), 0)) continue;
                    size_t size = 0;
                    void* bytes = mz_zip_reader_extract_file_to_heap(&zip, "godot/antares-ui-chrome.zip", &size, 0);
                    mz_zip_reader_end(&zip);
                    if (bytes == nullptr) continue;
                    pack.assign(static_cast<uint8_t*>(bytes), static_cast<uint8_t*>(bytes) + size);
                    mz_free(bytes);
                } else {
                    // A package folder (development builds): its pack beside it.
                    size_t size = 0;
                    void* bytes = SDL_LoadFile((data_dir + "mods/antares-ui-chrome/godot/antares-ui-chrome.zip").c_str(), &size);
                    if (bytes == nullptr) continue;
                    pack.assign(static_cast<uint8_t*>(bytes), static_cast<uint8_t*>(bytes) + size);
                    SDL_free(bytes);
                }
                break;
            }
            if (pack.empty()) return false;
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_mem(&zip, pack.data(), pack.size(), 0)) return false;
            out->clear();
            for (const char* name : names) {
                size_t size = 0;
                const std::string path = std::string("packages/antares-ui-chrome/art/") + name;
                void* bytes = mz_zip_reader_extract_file_to_heap(&zip, path.c_str(), &size, 0);
                if (bytes == nullptr) break;
                out->emplace_back(static_cast<uint8_t*>(bytes), static_cast<uint8_t*>(bytes) + size);
                mz_free(bytes);
            }
            mz_zip_reader_end(&zip);
            return out->size() == 3;
        }
    } // namespace

    bool Load(SDL_Renderer* renderer, const std::string& data_dir) {
#if defined(ANTARES_LAUNCHER_EMBEDDED_ART)
        (void)data_dir;
        bool ok = Load_Texture_(renderer, k_chrome_panel_data, k_chrome_panel_size, &s_panel);
        ok = Load_Texture_(renderer, k_chrome_bezel_data, k_chrome_bezel_size, &s_bezel) && ok;
        ok = Load_Texture_(renderer, k_chrome_button_data, k_chrome_button_size, &s_button) && ok;
#else
        static std::vector<std::vector<uint8_t>> art; // kept: the textures are made from it once
        bool ok = Package_Art_(data_dir, &art);
        if (ok) {
            ok = Load_Texture_(renderer, art[0].data(), art[0].size(), &s_panel);
            ok = Load_Texture_(renderer, art[1].data(), art[1].size(), &s_bezel) && ok;
            ok = Load_Texture_(renderer, art[2].data(), art[2].size(), &s_button) && ok;
        }
#endif
        s_russo = Load_Font_(k_chrome_russo_data, k_chrome_russo_size);
        s_saira = Load_Font_(k_chrome_saira_data, k_chrome_saira_size);
        s_saira_bold = Load_Font_(k_chrome_saira_bold_data, k_chrome_saira_bold_size);
        s_ready = ok && s_russo != nullptr && s_saira != nullptr && s_saira_bold != nullptr;
        if (!s_ready) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "launcher: the Chrome look didn't load: %s", SDL_GetError());
        return s_ready;
    }

    void Unload() {
        for (s_texture* texture : {&s_panel, &s_bezel, &s_button}) {
            if (texture->texture != nullptr) SDL_DestroyTexture(texture->texture);
            *texture = {};
        }
        s_ready = false;
    }

    bool Ready() {
        return s_ready;
    }

    namespace {
        e_look s_look = LOOK_CHROME;
        const ImVec4 STEEL(118 / 255.0f, 124 / 255.0f, 140 / 255.0f, 1.0f);
    } // namespace

    void Set_Look(e_look look) {
        s_look = look;
    }

    e_look Look() {
        return s_look;
    }

    void Style_Look(ImGuiStyle* style) {
        const bool glass = s_look == LOOK_GLASS;
        style->FrameRounding = glass ? 4.0f : 0.0f;
        style->ChildRounding = glass ? 6.0f : 0.0f;
        style->PopupRounding = glass ? 6.0f : 0.0f;
        style->GrabRounding = glass ? 3.0f : 0.0f;
        style->TabRounding = glass ? 5.0f : 0.0f;
    }

    ImFont* Title_Font() {
        return s_look == LOOK_GLASS && s_saira_bold != nullptr ? s_saira_bold : s_russo; // Glass's headings
    }
    ImFont* Body_Font() {
        return s_saira;
    }
    ImFont* Bold_Font() {
        return s_saira_bold;
    }

    void Background(ImDrawList* draw, ImVec2 min, ImVec2 max) {
        if (s_look == LOOK_GLASS) {
            // Deep space behind the glass: a dark navy fall and a sparse, fixed field of stars.
            draw->AddRectFilledMultiColor(min, max, IM_COL32(8, 10, 18, 255), IM_COL32(8, 10, 18, 255), IM_COL32(16, 20, 34, 255), IM_COL32(16, 20, 34, 255));
            uint32_t seed = 2166136261u;
            const float width = max.x - min.x, height = max.y - min.y;
            const int stars = static_cast<int>(width * height / 3500.0f);
            for (int i = 0; i < stars; ++i) {
                seed = seed * 1664525u + 1013904223u;
                const float x = min.x + static_cast<float>(seed % 10000u) / 10000.0f * width;
                seed = seed * 1664525u + 1013904223u;
                const float y = min.y + static_cast<float>(seed % 10000u) / 10000.0f * height;
                seed = seed * 1664525u + 1013904223u;
                const int light = 70 + static_cast<int>(seed % 150u);
                const float r = (seed % 23u) == 0 ? 1.4f : 0.7f;
                draw->AddCircleFilled(ImVec2(x, y), r, IM_COL32(light, light, std::min(255, light + 20), 255), 6);
            }
            return;
        }
        draw->AddRectFilled(min, max, IM_COL32(11, 12, 16, 255));
        if (s_panel.texture == nullptr) return;
        // Cover the rectangle, centred: crop the art by its coordinates rather than drawing past
        // the window's edge (SDL's software renderer drops triangles that hang off it).
        const float width = max.x - min.x, height = max.y - min.y;
        const float scale = std::max(width / s_panel.width, height / s_panel.height);
        const float u = (width / scale) / s_panel.width, v = (height / scale) / s_panel.height;
        draw->AddImage(Ref_(s_panel), min, max, ImVec2((1.0f - u) * 0.5f, (1.0f - v) * 0.5f), ImVec2((1.0f + u) * 0.5f, (1.0f + v) * 0.5f));
    }

    void Bezel(ImDrawList* draw, ImVec2 min, ImVec2 max) {
        if (s_look == LOOK_GLASS) { // the theme's Pane: dark glass, a thin steel edge
            draw->AddRectFilled(min, max, IM_COL32(10, 13, 22, 227), 8.0f);
            draw->AddRect(min, max, Colour_(STEEL, 0.85f), 8.0f, 0, 1.0f);
            return;
        }
        const float m = BEZEL_MARGIN;
        const float w = s_bezel.width, h = s_bezel.height;
        const float d = m * BEZEL_SCALE;
        const float xs[4] = {min.x, min.x + d, max.x - d, max.x};
        const float ys[4] = {min.y, min.y + d, max.y - d, max.y};
        const float us[4] = {0.0f, m, w - m, w};
        const float vs[4] = {0.0f, m, h - m, h};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                Slice_(draw, s_bezel, ImVec2(xs[column], ys[row]), ImVec2(xs[column + 1], ys[row + 1]), ImVec2(us[column], vs[row]), ImVec2(us[column + 1], vs[row + 1]), IM_COL32_WHITE);
            }
        }
    }

    bool Button(const char* id_and_label, ImVec2 size, bool main, bool enabled) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + size.x, min.y + size.y);
        ImGui::BeginDisabled(!enabled);
        const bool pressed = ImGui::InvisibleButton(id_and_label, size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        ImGui::EndDisabled();
        if (main && enabled) {
            for (int ring = 4; ring >= 1; --ring) { // a soft amber glow
                const float grow = static_cast<float>(ring) * 2.0f;
                draw->AddRectFilled(ImVec2(min.x - grow, min.y - grow), ImVec2(max.x + grow, max.y + grow), Colour_(AMBER, 0.07f), 6.0f + grow);
            }
        }
        if (s_look == LOOK_GLASS) { // the theme's buttons: flat glass, amber underline when pressed
            ImU32 fill = IM_COL32(28, 32, 44, 235), edge = Colour_(STEEL);
            if (!enabled) {
                fill = IM_COL32(18, 20, 26, 200);
                edge = IM_COL32(60, 62, 70, 255);
            } else if (held) {
                fill = IM_COL32(40, 26, 10, 240);
                edge = Colour_(AMBER);
            } else if (hovered) {
                fill = IM_COL32(40, 46, 62, 240);
                edge = IM_COL32(170, 176, 192, 255);
            } else if (main) {
                edge = Colour_(AMBER, 0.8f);
            }
            draw->AddRectFilled(min, max, fill, 5.0f);
            draw->AddRect(min, max, edge, 5.0f, 0, 1.0f);
            if (held) draw->AddLine(ImVec2(min.x + 4.0f, max.y - 1.0f), ImVec2(max.x - 4.0f, max.y - 1.0f), Colour_(AMBER), 2.0f);
            std::string label = id_and_label;
            label = label.substr(0, label.find("##"));
            for (char& character : label) character = static_cast<char>(SDL_toupper(static_cast<unsigned char>(character)));
            ImFont* font = s_saira_bold != nullptr ? s_saira_bold : s_russo;
            const float font_size = 17.0f;
            const ImVec2 text = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str());
            const ImVec4 colour = !enabled ? DIM : main ? AMBER
                                                        : LIGHT;
            draw->AddText(font, font_size, ImVec2(min.x + (size.x - text.x) * 0.5f, min.y + (size.y - text.y) * 0.5f), Colour_(colour, enabled ? 1.0f : 0.6f), label.c_str());
            return pressed && enabled;
        }
        const float cap = BUTTON_CAP * size.y / s_button.height * 0.62f;
        const float w = s_button.width, h = s_button.height;
        const int shade = !enabled ? 120 : held ? 200
            : hovered                           ? 255
                                                : 225;
        const ImU32 tint = IM_COL32(shade, shade, shade, 255);
        Slice_(draw, s_button, min, ImVec2(min.x + cap, max.y), ImVec2(0.0f, 0.0f), ImVec2(BUTTON_CAP, h), tint);
        Slice_(draw, s_button, ImVec2(min.x + cap, min.y), ImVec2(max.x - cap, max.y), ImVec2(BUTTON_CAP, 0.0f), ImVec2(w - BUTTON_CAP, h), tint);
        Slice_(draw, s_button, ImVec2(max.x - cap, min.y), max, ImVec2(w - BUTTON_CAP, 0.0f), ImVec2(w, h), tint);

        // The label: the visible part of "label##id", in capitals.
        std::string label = id_and_label;
        label = label.substr(0, label.find("##"));
        for (char& character : label) character = static_cast<char>(SDL_toupper(static_cast<unsigned char>(character)));
        const float font_size = 16.0f;
        const ImVec2 text = s_russo->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str());
        const ImVec2 at(min.x + (size.x - text.x) * 0.5f, min.y + (size.y - text.y) * 0.5f);
        const ImVec4 colour = !enabled ? DIM : main ? AMBER
                                                    : LIGHT;
        Shadowed_Text(draw, s_russo, font_size, at, ImVec4(colour.x, colour.y, colour.z, enabled ? 1.0f : 0.6f), label.c_str());
        return pressed && enabled;
    }

    void Shadowed_Text(ImDrawList* draw, ImFont* font, float size, ImVec2 at, const ImVec4& colour, const char* text) {
        draw->AddText(font, size, ImVec2(at.x, at.y + 2.0f), IM_COL32(0, 0, 0, 220), text);
        draw->AddText(font, size, at, Colour_(colour), text);
    }

    void Check_Box(ImDrawList* draw, ImVec2 min, float size, bool ok, bool warning) {
        const ImVec2 max(min.x + size, min.y + size);
        draw->AddRectFilled(min, max, IM_COL32(27, 29, 34, 255));
        draw->AddRect(min, max, IM_COL32(90, 92, 98, 255));
        const ImU32 colour = Colour_(ok ? GREEN : warning ? AMBER
                                                          : RED);
        const float s = size;
        if (ok) {
            draw->AddPolyline(std::array<ImVec2, 3>{ImVec2(min.x + s * 0.25f, min.y + s * 0.52f), ImVec2(min.x + s * 0.43f, min.y + s * 0.70f), ImVec2(min.x + s * 0.76f, min.y + s * 0.32f)}
                                  .data(),
                3,
                colour,
                ImDrawFlags_None,
                2.2f);
        } else if (warning) {
            draw->AddLine(ImVec2(min.x + s * 0.5f, min.y + s * 0.24f), ImVec2(min.x + s * 0.5f, min.y + s * 0.6f), colour, 2.4f);
            draw->AddCircleFilled(ImVec2(min.x + s * 0.5f, min.y + s * 0.74f), 1.6f, colour);
        } else {
            draw->AddLine(ImVec2(min.x + s * 0.3f, min.y + s * 0.3f), ImVec2(min.x + s * 0.7f, min.y + s * 0.7f), colour, 2.2f);
            draw->AddLine(ImVec2(min.x + s * 0.7f, min.y + s * 0.3f), ImVec2(min.x + s * 0.3f, min.y + s * 0.7f), colour, 2.2f);
        }
    }
} // namespace launcher::chrome
